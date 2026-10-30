// Fetches the usage snapshot: Claude Code OAuth first, claude.ai sessionKey as fallback.
#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "core/api.hpp"
#include "core/credentials.hpp"
#include "win/config.hpp"
#include "win/http_client.hpp"

namespace cub::win {

struct AppPaths {
    std::filesystem::path data_dir;     ///< %LOCALAPPDATA%\ClaudeUsageBar
    std::filesystem::path config;       ///< config.ini
    std::filesystem::path log;          ///< claude_usage_bar.log
    std::filesystem::path session_key;  ///< session.bin (DPAPI)
};

struct FetchResult {
    std::optional<api::UsageSnapshot> snapshot;
    std::string source;  ///< "OAuth" | "Web"
    std::string error;   ///< empty on success; short, user-facing, never contains secrets
};

/// Used from the worker thread only.
class UsageFetcher {
public:
    UsageFetcher(Config cfg, AppPaths paths);

    [[nodiscard]] FetchResult fetch();

private:
    [[nodiscard]] std::filesystem::path credentials_path() const;
    std::optional<api::UsageSnapshot> fetch_oauth(std::string& err);
    std::optional<credentials::OAuthCredentials> refresh_oauth(
        const credentials::OAuthCredentials& current, std::string& err);
    std::optional<api::UsageSnapshot> fetch_session(std::string& err);

    Config cfg_;
    AppPaths paths_;
    HttpClient http_;                // honest tool UA, api.anthropic.com
    std::optional<HttpClient> web_;  // browser UA, claude.ai (lazy)
    std::wstring org_id_;
};

}  // namespace cub::win
