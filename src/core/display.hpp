// Turns a usage snapshot into the two taskbar lines. Pure logic, UTF-8 output.
#pragma once

#include <cstdint>
#include <string>

#include <span>

#include "core/api.hpp"
#include "core/usage_math.hpp"

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

/// Hover text for the widget (lines separated by '\n'):
///   Oturum %23 · tempo 5 puan önde (beklenen %18)
///   Reset 21:40 (2s14dk) · %100 tahmini 20:15
///   Hafta %41 · tempo 3 puan geride (beklenen %44)
///   Reset Pzt 10:00 (3g05s)
///   Sol tık: grafik raporu · Sağ tık: menü
/// `recent` are the latest CSV samples (session projection only).
[[nodiscard]] std::string tooltip(const api::UsageSnapshot& snap,
                                  std::span<const usage::Sample> recent, std::int64_t now,
                                  std::int32_t utc_offset_sec);

}  // namespace cub::display
