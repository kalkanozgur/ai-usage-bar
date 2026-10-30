#include "core/display.hpp"

#include <cmath>

#include "core/time_util.hpp"
#include "core/usage_math.hpp"

namespace cub::display {
namespace {

constexpr const char* kSep = " \xC2\xB7 ";  // " · "
constexpr const char* kDash = "\xE2\x80\x94";  // "—"

std::string pct_text(i18n::Lang lang, double pct) {
    return i18n::pct(lang, static_cast<double>(std::lround(pct)));
}

std::string window_text(i18n::Lang lang, i18n::S label, const std::optional<api::UsageWindow>& w,
                        std::int64_t now) {
    std::string s = i18n::t(lang, label);
    s += ' ';
    if (!w) {
        return s + kDash;
    }
    s += pct_text(lang, w->utilization);
    s += kSep;
    s += w->resets_at ? i18n::remaining(lang, *w->resets_at - now) : kDash;
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

Level level_for_window(const api::UsageWindow& w, std::int64_t window_sec, std::int64_t now,
                       const Options& opt) noexcept {
    if (w.utilization >= opt.crit_pct) {
        return Level::Critical;
    }
    if (opt.color_mode == ColorMode::Threshold || !w.resets_at) {
        return level_for(w.utilization, opt);
    }
    const auto p = usage::pace(w.utilization, *w.resets_at, window_sec, now);
    if (p && w.utilization >= opt.pace_min_pct && p->delta_pct >= opt.pace_margin) {
        return Level::Warn;
    }
    return Level::Normal;
}

Lines format(const api::UsageSnapshot& snap, std::int64_t now, const Options& opt, bool stale) {
    Lines out;
    out.line1 = window_text(opt.lang, i18n::S::Session, snap.five_hour, now);
    out.line2 = window_text(opt.lang, i18n::S::Week, snap.seven_day, now);
    if (opt.show_sonnet && snap.seven_day_sonnet) {
        out.line2 += kSep;
        out.line2 += "S " + pct_text(opt.lang, snap.seven_day_sonnet->utilization);
    }
    if (stale) {
        out.level1 = out.level2 = Level::Dim;
    } else {
        out.level1 = snap.five_hour ? level_for_window(*snap.five_hour, usage::kSessionWindowSec,
                                                        now, opt)
                                    : Level::Dim;
        out.level2 = snap.seven_day
                         ? level_for_window(*snap.seven_day, usage::kWeekWindowSec, now, opt)
                         : Level::Dim;
    }
    return out;
}

}  // namespace cub::display
