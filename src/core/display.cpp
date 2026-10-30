#include "core/display.hpp"

#include <cmath>

#include "core/time_util.hpp"

namespace cub::display {
namespace {

constexpr const char* kSep = " \xC2\xB7 ";  // " · "
constexpr const char* kDash = "\xE2\x80\x94";  // "—"

std::string pct_text(double pct) {
    return "%" + std::to_string(std::lround(pct));
}

std::string window_text(const char* label, const std::optional<api::UsageWindow>& w,
                        std::int64_t now) {
    std::string s = label;
    s += ' ';
    if (!w) {
        return s + kDash;
    }
    s += pct_text(w->utilization);
    s += kSep;
    s += w->resets_at ? time::format_remaining(*w->resets_at - now) : kDash;
    return s;
}

}  // namespace

Level level_for(double pct, const Options& opt) noexcept {
    if (pct >= opt.crit_pct) {
        return Level::Critical;
    }
    if (pct >= opt.warn_pct) {
        return Level::Warn;
    }
    return Level::Normal;
}

Lines format(const api::UsageSnapshot& snap, std::int64_t now, const Options& opt, bool stale) {
    Lines out;
    out.line1 = window_text("Oturum", snap.five_hour, now);
    out.line2 = window_text("Hafta", snap.seven_day, now);
    if (opt.show_sonnet && snap.seven_day_sonnet) {
        out.line2 += kSep;
        out.line2 += "S " + pct_text(snap.seven_day_sonnet->utilization);
    }
    if (stale) {
        out.level1 = out.level2 = Level::Dim;
    } else {
        out.level1 = snap.five_hour ? level_for(snap.five_hour->utilization, opt) : Level::Dim;
        out.level2 = snap.seven_day ? level_for(snap.seven_day->utilization, opt) : Level::Dim;
    }
    return out;
}

namespace {

std::string window_tip(const char* label, const std::optional<api::UsageWindow>& w,
                       std::int64_t window_sec, std::int64_t now, std::int32_t off,
                       const std::optional<std::int64_t>& full) {
    std::string s = label;
    s += ' ';
    if (!w) {
        return s + kDash;
    }
    s += pct_text(w->utilization);
    if (!w->resets_at) {
        return s;
    }
    if (const auto p = usage::pace(w->utilization, *w->resets_at, window_sec, now)) {
        const long d = std::lround(p->delta_pct);
        s += kSep;
        s += d == 0 ? std::string("tempo dengede")
                    : "tempo " + std::to_string(d < 0 ? -d : d) + " puan " +
                          (d > 0 ? "\xC3\xB6nde" : "geride");
        s += " (beklenen " + pct_text(p->expected_pct) + ")";
    }
    s += "\nReset " + time::format_local_clock(*w->resets_at, now, off) + " (" +
         time::format_remaining(*w->resets_at - now) + ")";
    if (full) {
        s += kSep;
        s += "%100 tahmini " + time::format_local_clock(*full, now, off);
    }
    return s;
}

}  // namespace

std::string tooltip(const api::UsageSnapshot& snap, std::span<const usage::Sample> recent,
                    std::int64_t now, std::int32_t off) {
    const auto full = usage::projected_full(recent, usage::Window::Session);
    std::string s = window_tip("Oturum", snap.five_hour, usage::kSessionWindowSec, now, off, full);
    s += '\n';
    s += window_tip("Hafta", snap.seven_day, usage::kWeekWindowSec, now, off, std::nullopt);
    s += "\nSol t\xC4\xB1k: grafik raporu \xC2\xB7 Sa\xC4\x9F t\xC4\xB1k: men\xC3\xBC";
    return s;
}

}  // namespace cub::display
