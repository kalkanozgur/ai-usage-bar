// Limit time series: the samples the widget logs to CSV and the math the chart shows.
//
//   CSV row:  ts,session_pct,session_reset,week_pct,week_reset
//             1793383200,23.0,1793397600,41.0,1793800000
//   Empty field = value was missing in that API response. Only numbers are stored.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/api.hpp"

namespace cub::usage {

inline constexpr std::int64_t kSessionWindowSec = 5 * 3600;
inline constexpr std::int64_t kWeekWindowSec = 7 * 24 * 3600;
/// resets_at may jitter between responses; samples within this tolerance share a window.
inline constexpr std::int64_t kResetToleranceSec = 120;

struct Sample {
    std::int64_t ts = 0;  ///< Unix seconds
    std::optional<double> session_pct;
    std::optional<std::int64_t> session_reset;
    std::optional<double> week_pct;
    std::optional<std::int64_t> week_reset;
};

[[nodiscard]] Sample from_snapshot(std::int64_t now, const api::UsageSnapshot& snap) noexcept;

// ---- CSV -------------------------------------------------------------------

[[nodiscard]] std::string_view csv_header() noexcept;
/// One row without line terminator; locale independent.
[[nodiscard]] std::string to_csv_row(const Sample& s);
/// nullopt for the header, blank or malformed lines. Tolerates a trailing '\r'.
[[nodiscard]] std::optional<Sample> parse_csv_row(std::string_view line);
/// Monthly file name for a sample time (UTC): "usage-2026-10.csv".
[[nodiscard]] std::string csv_file_name(std::int64_t ts);
/// True for "usage-YYYY-MM.csv" files older than `keep_months` calendar months (UTC).
/// keep_months <= 0 keeps everything; values below 2 are raised to 2 so the report's
/// 8-day lookback always finds the previous month.
[[nodiscard]] bool csv_expired(std::string_view file_name, std::int64_t now,
                               int keep_months) noexcept;
/// Appends one row, writing the header first when the file is new or empty.
bool append_csv(const std::filesystem::path& file, const Sample& s);

// ---- analysis --------------------------------------------------------------

/// Tempo: where utilization "should" be if spent evenly across the window.
struct Pace {
    double expected_pct = 0.0;  ///< 100 × elapsed / window
    double delta_pct = 0.0;     ///< utilization − expected; > 0 means ahead of pace
};

[[nodiscard]] std::optional<Pace> pace(double utilization, std::int64_t resets_at,
                                       std::int64_t window_sec, std::int64_t now) noexcept;

enum class Window { Session, Week };

/// Least-squares projection over the latest window's samples: Unix time at which the
/// window reaches 100 %. nullopt if usage is flat, data is too thin, or the limit
/// would only be reached after the window resets.
[[nodiscard]] std::optional<std::int64_t> projected_full(std::span<const Sample> samples,
                                                         Window w) noexcept;

/// Session → week exchange rate: weekly points consumed per session point,
/// Σ Δweek / Σ Δsession over consecutive samples inside the same windows.
/// nullopt until at least 1 session point has been observed.
[[nodiscard]] std::optional<double> session_to_week_rate(std::span<const Sample> samples) noexcept;

}  // namespace cub::usage
