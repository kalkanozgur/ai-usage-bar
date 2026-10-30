// Turns a usage snapshot into the two taskbar lines. Pure logic, UTF-8 output.
#pragma once

#include <cstdint>
#include <string>

#include "core/api.hpp"

namespace cub::display {

enum class Level { Normal, Warn, Critical, Dim };

struct Options {
    double warn_pct = 75.0;
    double crit_pct = 90.0;
    bool show_sonnet = false;
};

struct Lines {
    std::string line1;
    std::string line2;
    Level level1 = Level::Normal;
    Level level2 = Level::Normal;

    bool operator==(const Lines&) const = default;
};

[[nodiscard]] Level level_for(double pct, const Options& opt) noexcept;

/// "Oturum %23 · 2s14dk" / "Hafta %41 · 3g05s". `stale` dims both lines.
[[nodiscard]] Lines format(const api::UsageSnapshot& snap, std::int64_t now_unix,
                           const Options& opt, bool stale);

}  // namespace cub::display
