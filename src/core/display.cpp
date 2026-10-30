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

}  // namespace cub::display
