// Self-contained HTML report (inline SVG + CSS, no scripts, no network) for the left-click view.
//
// Views (radio tabs, default "Bu hafta"):
//   Son oturum · Bugün · Bu hafta
// Each view: limit time series (session % + week %), token breakdown by group, session table.
// Only numbers, tool-group names and project folder names appear; no transcript text.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/usage_breakdown.hpp"
#include "core/usage_math.hpp"

namespace cub::report {

enum class View { LastSession, Today, Week };

struct Input {
    std::int64_t now = 0;              ///< Unix seconds
    std::int32_t utc_offset_sec = 0;   ///< local time = UTC + offset (labels only)
    std::vector<usage::Sample> samples;           ///< ascending by ts; at least the last 7 days
    std::vector<breakdown::Result> sessions;      ///< any order; ended_at decides the view
    View default_view = View::Week;
};

[[nodiscard]] std::string render_html(const Input& in);

// Exposed for tests.
[[nodiscard]] std::string escape_html(std::string_view s);
/// 812 → "812", 81200 → "81,2 K", 3040000 → "3,04 M".
[[nodiscard]] std::string format_tokens(double v);
/// Aggregates group costs of several sessions; largest first, shares over the combined total.
[[nodiscard]] breakdown::Result merge(const std::vector<const breakdown::Result*>& parts);

}  // namespace cub::report
