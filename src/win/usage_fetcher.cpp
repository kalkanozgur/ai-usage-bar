#include "win/usage_fetcher.hpp"

#include <windows.h>

#include <array>
#include <system_error>

#include "version.h"
#include "win/log.hpp"
#include "win/process_watch.hpp"
#include "win/secret_store.hpp"
#include "win/util.hpp"

namespace cub::win {
namespace {

#define L_(key) i18n::t(cfg_.lang, i18n::S::key)

// Public OAuth client of Claude Code; the token we refresh was issued to it.
constexpr std::string_view kClaudeCodeClientId = "9d1c250a-e61b-44d9-88ed-5944d1962f5e";
constexpr std::array<const wchar_t*, 2> kTokenHosts = {L"platform.claude.com",
                                                       L"console.anthropic.com"};
constexpr std::int64_t kRefreshSkewMs = 5 * 60 * 1000;

constexpr const wchar_t* kBrowserUa =
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
    L"Chrome/130.0 Safari/537.36";

std::wstring tool_user_agent() { return L"ClaudeUsageBar/" CUB_VERSION_WSTRING; }

template <class Str>
void wipe(Str& s) noexcept {
    if (!s.empty()) {
        SecureZeroMemory(s.data(), s.size() * sizeof(typename Str::value_type));
    }
}

}  // namespace

UsageFetcher::UsageFetcher(Config cfg, AppPaths paths)
    : cfg_(std::move(cfg)), paths_(std::move(paths)), http_(tool_user_agent()),
      org_id_(cfg_.org_id) {}

std::filesystem::path UsageFetcher::credentials_path() const {
    return claude_config_dir() / L".credentials.json";
}

FetchResult UsageFetcher::fetch() {
    FetchResult r;
    std::string oauth_err;
    std::string web_err;
    if (cfg_.auth != Config::AuthMode::Session) {
        if (auto s = fetch_oauth(oauth_err)) {
            r.snapshot = std::move(s);
            r.source = "OAuth";
            return r;
        }
    }
    if (cfg_.auth != Config::AuthMode::OAuth) {
        if (auto s = fetch_session(web_err)) {
            r.snapshot = std::move(s);
            r.source = "Web";
            return r;
        }
    }
    r.error = oauth_err;
    if (!web_err.empty()) {
        r.error += r.error.empty() ? web_err : " | " + web_err;
    }
    if (r.error.empty()) {
        r.error = "kimlik bilgisi yok";
    }
    log::write("fetch failed: " + r.error);
    return r;
}

std::optional<api::UsageSnapshot> UsageFetcher::fetch_oauth(std::string& err) {
    const auto text = read_file(credentials_path());
    if (!text) {
        err = L_(ErrOAuthNoLogin);
        return std::nullopt;
    }
    auto creds = credentials::parse(*text);
    if (!creds) {
        err = L_(ErrOAuthCredRead);
        return std::nullopt;
    }

    bool refreshed = false;
    auto may_refresh = [&] {
        // Rotating the refresh token under a running CLI would log it out, so only refresh
        // while it is closed. A running CLI refreshes the token on its own anyway.
        return cfg_.oauth_refresh && !creds->refresh_token.empty() && !refreshed &&
               !claude_cli_running(cfg_.desktop_path_hint);
    };

    if (credentials::needs_refresh(*creds, now_unix_ms(), kRefreshSkewMs)) {
        if (!may_refresh()) {
            err = L_(ErrOAuthExpired);
            return std::nullopt;
        }
        auto fresh = refresh_oauth(*creds, err);
        if (!fresh) {
            return std::nullopt;
        }
        creds = std::move(fresh);
        refreshed = true;
    }

    for (int attempt = 0; attempt < 2; ++attempt) {
        std::wstring headers = L"Authorization: Bearer " + to_wide(creds->access_token) +
                               L"\r\nanthropic-beta: oauth-2025-04-20\r\nAccept: application/json";
        std::string terr;
        const auto resp =
            http_.request(L"GET", L"api.anthropic.com", L"/api/oauth/usage", headers, {}, &terr);
        wipe(headers);
        if (!resp) {
            err = "OAuth: " + terr;
            return std::nullopt;
        }
        if (resp->status == 200) {
            auto snap = api::parse_usage(resp->body);
            if (!snap) {
                err = std::string("OAuth: ") + L_(ErrUnexpected);
            }
            return snap;
        }
        if (resp->status == 401 && may_refresh()) {
            auto fresh = refresh_oauth(*creds, err);
            if (!fresh) {
                return std::nullopt;
            }
            creds = std::move(fresh);
            refreshed = true;
            continue;
        }
        err = "OAuth: HTTP " + std::to_string(resp->status);
        return std::nullopt;
    }
    err = L_(ErrOAuthDenied);
    return std::nullopt;
}

std::optional<credentials::OAuthCredentials> UsageFetcher::refresh_oauth(
    const credentials::OAuthCredentials& current, std::string& err) {
    std::string body = credentials::build_refresh_request(current.refresh_token, kClaudeCodeClientId);
    std::string last_err = L_(ErrUnreachable);

    for (const wchar_t* host : kTokenHosts) {
        std::string terr;
        const auto resp = http_.request(L"POST", host, L"/v1/oauth/token",
                                        L"Content-Type: application/json\r\nAccept: application/json",
                                        body, &terr);
        if (!resp) {
            last_err = terr;
            continue;
        }
        if (resp->status == 404 || resp->status >= 500) {
            last_err = "HTTP " + std::to_string(resp->status);
            continue;  // try the legacy host
        }
        wipe(body);
        if (resp->status != 200) {
            // 400 invalid_grant: refresh token revoked/expired → user must run `claude` and log in.
            err = std::string(L_(ErrRefreshPrefix)) + "HTTP " + std::to_string(resp->status);
            return std::nullopt;
        }
        const auto parsed = credentials::parse_refresh_response(resp->body);
        if (!parsed) {
            err = std::string(L_(ErrRefreshPrefix)) + L_(ErrUnexpected);
            return std::nullopt;
        }
        auto fresh = credentials::apply_refresh(current, *parsed, now_unix_ms());

        // Persist so Claude Code keeps working with the rotated refresh token.
        const auto path = credentials_path();
        const auto latest = read_file(path);
        const auto on_disk = latest ? credentials::parse(*latest) : std::nullopt;
        if (on_disk && on_disk->refresh_token != current.refresh_token) {
            log::write("credentials changed on disk during refresh; leaving file untouched");
            return fresh;
        }
        if (latest) {
            std::filesystem::path backup = path;
            backup += L".cub-backup";
            std::error_code ec;
            if (!std::filesystem::exists(backup, ec)) {
                std::filesystem::copy_file(path, backup, ec);
            }
            if (const auto patched = credentials::patch(*latest, fresh);
                !patched || !write_file_atomic(path, *patched)) {
                log::write("could not write refreshed credentials");
            } else {
                log::write("OAuth token refreshed");
            }
        }
        return fresh;
    }
    wipe(body);
    err = L_(ErrRefreshPrefix) + last_err;
    return std::nullopt;
}

std::optional<api::UsageSnapshot> UsageFetcher::fetch_session(std::string& err) {
    auto key = secret::load(paths_.session_key);
    if (!key || key->empty()) {
        err = L_(ErrWebNoKey);
        return std::nullopt;
    }
    if (!web_) {
        web_.emplace(kBrowserUa);
    }
    std::wstring headers = L"Cookie: sessionKey=" + to_wide(*key) +
                           L"\r\nAccept: application/json"
                           L"\r\nReferer: https://claude.ai/settings/usage";
    wipe(*key);

    struct Wiper {
        std::wstring& h;
        ~Wiper() { wipe(h); }
    } wiper{headers};

    std::string terr;
    if (org_id_.empty()) {
        const auto resp =
            web_->request(L"GET", L"claude.ai", L"/api/organizations", headers, {}, &terr);
        if (!resp) {
            err = "Web: " + terr;
            return std::nullopt;
        }
        if (resp->status != 200) {
            err = "Web: HTTP " + std::to_string(resp->status) +
                  (resp->status == 403 ? L_(ErrCookieOrCloudflare) : "");
            return std::nullopt;
        }
        const auto org = api::pick_chat_org(resp->body);
        if (!org) {
            err = L_(ErrWebNoOrg);
            return std::nullopt;
        }
        org_id_ = to_wide(*org);
        save_org_id(paths_.config, org_id_);
    }

    const auto resp = web_->request(L"GET", L"claude.ai",
                                    L"/api/organizations/" + org_id_ + L"/usage", headers, {}, &terr);
    if (!resp) {
        err = "Web: " + terr;
        return std::nullopt;
    }
    if (resp->status != 200) {
        if (resp->status == 404) {
            org_id_.clear();  // stale cached org: rediscover next time
        }
        err = "Web: HTTP " + std::to_string(resp->status) +
              (resp->status == 403 ? L_(ErrCookieOrCloudflare) : "");
        return std::nullopt;
    }
    auto snap = api::parse_usage(resp->body);
    if (!snap) {
        err = std::string("Web: ") + L_(ErrUnexpected);
    }
    return snap;
}

#undef L_

}  // namespace cub::win
