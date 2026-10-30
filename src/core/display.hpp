// Turns a usage snapshot into the two taskbar lines (English). Pure logic, UTF-8 output.
#pragma once

#include <cstdint>
#include <string>

#include "core/api.hpp"

namespace cub::display {

enum class Level { Normal, Warn, Critical, Dim };

enum class ColorMode {
    Pace,       ///< orange when on track to hit 100 % before the reset, red at crit_pct
    Threshold,  ///< orange at warn_pct, red at crit_pct
};

struct Options {
    double warn_pct = 75.0;  ///< Threshold mode only
    double crit_pct = 90.0;  ///< both modes
    bool show_sonnet = false;
    ColorMode color_mode = ColorMode::Pace;
    double pace_margin = 5.0;    ///< points ahead of even spend before turning orange
    double pace_min_pct = 25.0;  ///< ignore pace below this usage (early-window noise)
};

struct Lines {
    std::string line1;
    std::string line2;
    Level level1 = Level::Normal;
    Level level2 = Level::Normal;

    bool operator==(const Lines&) const = default;
};

/// Threshold-mode level (ignores pace).
[[nodiscard]] Level level_for(double pct, const Options& opt) noexcept;

/// Level of one window. Pace mode: Critical at crit_pct; Warn when usage ≥ pace_min_pct and
/// at least pace_margin points ahead of even spend (i.e. heading for 100 % before the reset);
/// falls back to thresholds when the reset time is unknown.
[[nodiscard]] Level level_for_window(const api::UsageWindow& w, std::int64_t window_sec,
                                     std::int64_t now, const Options& opt) noexcept;

/// "Session 23% · 2h14m" / "Week 41% · 3d05h". `stale` dims both lines.
[[nodiscard]] Lines format(const api::UsageSnapshot& snap, std::int64_t now_unix,
                           const Options& opt, bool stale);

}  // namespace cub::display
