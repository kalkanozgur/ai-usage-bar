#include <gtest/gtest.h>

#include "core/display.hpp"

using namespace cub;

namespace {

api::UsageSnapshot sample(std::int64_t now) {
    api::UsageSnapshot s;
    s.five_hour = api::UsageWindow{23.4, now + 2 * 3600 + 14 * 60};
    s.seven_day = api::UsageWindow{80.6, now + 3 * 86400 + 5 * 3600};
    s.seven_day_sonnet = api::UsageWindow{91.0, std::nullopt};
    return s;
}

}  // namespace

TEST(Display, FormatsBothLines) {
    const auto lines = display::format(sample(1000), 1000, {}, false);
    EXPECT_EQ(lines.line1, "Session 23% \xC2\xB7 2h14m");
    EXPECT_EQ(lines.line2, "Week 81% \xC2\xB7 3d05h");
    EXPECT_EQ(lines.level1, display::Level::Normal);
    EXPECT_EQ(lines.level2, display::Level::Warn);
}

TEST(Display, AppendsSonnetWhenEnabled) {
    display::Options opt;
    opt.show_sonnet = true;
    const auto lines = display::format(sample(0), 0, opt, false);
    EXPECT_EQ(lines.line2, "Week 81% \xC2\xB7 3d05h \xC2\xB7 S 91%");
}

TEST(Display, StaleDimsEverything) {
    const auto lines = display::format(sample(0), 0, {}, true);
    EXPECT_EQ(lines.level1, display::Level::Dim);
    EXPECT_EQ(lines.level2, display::Level::Dim);
}

TEST(Display, MissingWindowShowsDash) {
    api::UsageSnapshot s;
    s.seven_day = api::UsageWindow{10.0, std::nullopt};
    const auto lines = display::format(s, 0, {}, false);
    EXPECT_EQ(lines.line1, "Session \xE2\x80\x94");
    EXPECT_EQ(lines.line2, "Week 10% \xC2\xB7 \xE2\x80\x94");
    EXPECT_EQ(lines.level1, display::Level::Dim);
}

TEST(Display, Thresholds) {
    const display::Options opt{75, 90, false};
    EXPECT_EQ(display::level_for(74.9, opt), display::Level::Normal);
    EXPECT_EQ(display::level_for(75.0, opt), display::Level::Warn);
    EXPECT_EQ(display::level_for(90.0, opt), display::Level::Critical);
}

TEST(Display, PaceColoring) {
    const std::int64_t now = 100'000;
    display::Options opt;  // pace mode, margin 5, min 25, crit 90
    const std::int64_t half = 9000;  // half of the 5 h window left → expected 50 %
    // Ahead of pace → orange even below warn_pct
    EXPECT_EQ(display::level_for_window({60.0, now + half}, 18'000, now, opt),
              display::Level::Warn);
    // 71 % with 41 % of the window elapsed (expected 41 %) → far ahead → orange
    EXPECT_EQ(display::level_for_window({71.0, now + 10'620}, 18'000, now, opt),
              display::Level::Warn);
    // 71 % late in the window (expected ~81 %) → slow → stays white (user's case)
    EXPECT_EQ(display::level_for_window({71.0, now + 3368}, 18'000, now, opt),
              display::Level::Normal);
    // Within the margin → white
    EXPECT_EQ(display::level_for_window({54.0, now + half}, 18'000, now, opt),
              display::Level::Normal);
    // Early-window noise below pace_min_pct → white
    EXPECT_EQ(display::level_for_window({10.0, now + 17'000}, 18'000, now, opt),
              display::Level::Normal);
    // Red at crit_pct regardless of pace
    EXPECT_EQ(display::level_for_window({92.0, now + 600}, 18'000, now, opt),
              display::Level::Critical);
    // Unknown reset → thresholds
    EXPECT_EQ(display::level_for_window({80.0, std::nullopt}, 18'000, now, opt),
              display::Level::Warn);
}

TEST(Display, ThresholdModeIgnoresPace) {
    const std::int64_t now = 100'000;
    display::Options opt;
    opt.color_mode = display::ColorMode::Threshold;
    EXPECT_EQ(display::level_for_window({60.0, now + 9000}, 18'000, now, opt),
              display::Level::Normal);
    EXPECT_EQ(display::level_for_window({75.0, now + 9000}, 18'000, now, opt),
              display::Level::Warn);
}
