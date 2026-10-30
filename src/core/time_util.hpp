#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cub::time {

/// Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant's algorithm).
[[nodiscard]] constexpr std::int64_t days_from_civil(std::int64_t y, unsigned m,
                                                     unsigned d) noexcept {
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

/// Parses "YYYY-MM-DDTHH:MM:SS[.frac][Z|+HH:MM|-HH:MM|+HHMM]" into Unix seconds (UTC).
/// A missing zone designator is treated as UTC.
[[nodiscard]] std::optional<std::int64_t> parse_iso8601(std::string_view s) noexcept;

/// Compact Turkish countdown: "2g05s", "3s07dk", "12dk". Negative input clamps to "0dk".
[[nodiscard]] std::string format_remaining(std::int64_t seconds);

}  // namespace cub::time
