#include "core/credentials.hpp"

#include <algorithm>
#include <array>

#include "core/json_lite.hpp"

namespace cub::credentials {
namespace {

constexpr std::string_view kRootKey = "claudeAiOauth";

std::optional<json::Span> oauth_object(std::string_view doc) {
    const auto root = json::root(doc);
    if (!root || !json::is_object(doc, *root)) {
        return std::nullopt;
    }
    const auto obj = json::member(doc, *root, kRootKey);
    if (!obj || !json::is_object(doc, *obj)) {
        return std::nullopt;
    }
    return obj;
}

}  // namespace

std::optional<OAuthCredentials> parse(std::string_view doc) {
    const auto obj = oauth_object(doc);
    if (!obj) {
        return std::nullopt;
    }
    OAuthCredentials c;
    if (const auto v = json::member(doc, *obj, "accessToken")) {
        c.access_token = json::as_string(doc, *v).value_or("");
    }
    if (const auto v = json::member(doc, *obj, "refreshToken")) {
        c.refresh_token = json::as_string(doc, *v).value_or("");
    }
    if (const auto v = json::member(doc, *obj, "expiresAt")) {
        c.expires_at_ms = static_cast<std::int64_t>(json::as_number(doc, *v).value_or(0.0));
    }
    if (c.access_token.empty()) {
        return std::nullopt;
    }
    return c;
}

std::optional<std::string> patch(std::string_view doc, const OAuthCredentials& creds) {
    const auto obj = oauth_object(doc);
    if (!obj) {
        return std::nullopt;
    }
    const auto access = json::member(doc, *obj, "accessToken");
    const auto refresh = json::member(doc, *obj, "refreshToken");
    const auto expires = json::member(doc, *obj, "expiresAt");
    if (!access || !refresh || !expires) {
        return std::nullopt;
    }

    struct Edit {
        json::Span span;
        std::string text;
    };
    std::array<Edit, 3> edits{{
        {*access, json::quote(creds.access_token)},
        {*refresh, json::quote(creds.refresh_token)},
        {*expires, std::to_string(creds.expires_at_ms)},
    }};
    // Apply back-to-front so earlier offsets stay valid.
    std::sort(edits.begin(), edits.end(),
              [](const Edit& a, const Edit& b) { return a.span.begin > b.span.begin; });

    std::string out(doc);
    for (const auto& e : edits) {
        out.replace(e.span.begin, e.span.size(), e.text);
    }
    return out;
}

bool needs_refresh(const OAuthCredentials& c, std::int64_t now_ms, std::int64_t skew_ms) noexcept {
    return c.expires_at_ms <= 0 || c.expires_at_ms - skew_ms <= now_ms;
}

std::string build_refresh_request(std::string_view refresh_token, std::string_view client_id) {
    std::string body = R"({"grant_type":"refresh_token","refresh_token":)";
    body += json::quote(refresh_token);
    body += R"(,"client_id":)";
    body += json::quote(client_id);
    body += '}';
    return body;
}

std::optional<RefreshResult> parse_refresh_response(std::string_view doc) {
    const auto root = json::root(doc);
    if (!root || !json::is_object(doc, *root)) {
        return std::nullopt;
    }
    RefreshResult r;
    if (const auto v = json::member(doc, *root, "access_token")) {
        r.access_token = json::as_string(doc, *v).value_or("");
    }
    if (r.access_token.empty()) {
        return std::nullopt;
    }
    if (const auto v = json::member(doc, *root, "refresh_token")) {
        if (auto s = json::as_string(doc, *v); s && !s->empty()) {
            r.refresh_token = std::move(*s);
        }
    }
    if (const auto v = json::member(doc, *root, "expires_in")) {
        r.expires_in_s = static_cast<std::int64_t>(json::as_number(doc, *v).value_or(0.0));
    }
    if (r.expires_in_s <= 0) {
        return std::nullopt;
    }
    return r;
}

OAuthCredentials apply_refresh(const OAuthCredentials& old, const RefreshResult& r,
                               std::int64_t now_ms) noexcept {
    OAuthCredentials c = old;
    c.access_token = r.access_token;
    if (r.refresh_token) {
        c.refresh_token = *r.refresh_token;
    }
    c.expires_at_ms = now_ms + r.expires_in_s * 1000;
    return c;
}

}  // namespace cub::credentials
