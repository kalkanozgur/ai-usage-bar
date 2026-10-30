// Parsers for the Claude usage endpoints.
//   OAuth: GET https://api.anthropic.com/api/oauth/usage
//   Web:   GET https://claude.ai/api/organizations/{org}/usage
// Both return the same shape:
//   { "five_hour": {"utilization": 23.0, "resets_at": "..."}, "seven_day": {...}, ... }
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cub::api {

struct UsageWindow {
    double utilization = 0.0;                 ///< 0..100
    std::optional<std::int64_t> resets_at;  ///< Unix seconds
};

struct UsageSnapshot {
    std::optional<UsageWindow> five_hour;
    std::optional<UsageWindow> seven_day;
    std::optional<UsageWindow> seven_day_sonnet;
    std::optional<UsageWindow> seven_day_opus;
};

/// Returns nullopt unless the document carries at least one of five_hour / seven_day.
[[nodiscard]] std::optional<UsageSnapshot> parse_usage(std::string_view json);

/// From GET https://claude.ai/api/organizations: first org whose "capabilities" contains
/// "chat", otherwise the first org with a uuid.
[[nodiscard]] std::optional<std::string> pick_chat_org(std::string_view json);

}  // namespace cub::api
