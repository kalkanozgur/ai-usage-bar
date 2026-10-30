// Claude Code OAuth credentials (~/.claude/.credentials.json):
//   {"claudeAiOauth":{"accessToken":"...","refreshToken":"...","expiresAt":1767000000000,...}}
// The file belongs to Claude Code, so any write-back must leave every other byte untouched.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cub::credentials {

struct OAuthCredentials {
    std::string access_token;
    std::string refresh_token;
    std::int64_t expires_at_ms = 0;  ///< Unix epoch milliseconds; 0 = unknown
};

struct RefreshResult {
    std::string access_token;
    std::optional<std::string> refresh_token;  ///< absent → keep the previous one
    std::int64_t expires_in_s = 0;
};

[[nodiscard]] std::optional<OAuthCredentials> parse(std::string_view json);

/// Copy of `json` with accessToken, refreshToken and expiresAt replaced; nullopt if any is missing.
[[nodiscard]] std::optional<std::string> patch(std::string_view json,
                                               const OAuthCredentials& creds);

/// True when the access token expires within `skew_ms` (or the expiry is unknown).
[[nodiscard]] bool needs_refresh(const OAuthCredentials& c, std::int64_t now_ms,
                                 std::int64_t skew_ms) noexcept;

[[nodiscard]] std::string build_refresh_request(std::string_view refresh_token,
                                                std::string_view client_id);

[[nodiscard]] std::optional<RefreshResult> parse_refresh_response(std::string_view json);

/// Applies a refresh response on top of the credentials that were used for it.
[[nodiscard]] OAuthCredentials apply_refresh(const OAuthCredentials& old, const RefreshResult& r,
                                             std::int64_t now_ms) noexcept;

}  // namespace cub::credentials
