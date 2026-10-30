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
    EXPECT_EQ(lines.line1, "Oturum %23 \xC2\xB7 2s14dk");
    EXPECT_EQ(lines.line2, "Hafta %81 \xC2\xB7 3g05s");
    EXPECT_EQ(lines.level1, display::Level::Normal);
    EXPECT_EQ(lines.level2, display::Level::Warn);
}

TEST(Display, AppendsSonnetWhenEnabled) {
    display::Options opt;
    opt.show_sonnet = true;
    const auto lines = display::format(sample(0), 0, opt, false);
    EXPECT_EQ(lines.line2, "Hafta %81 \xC2\xB7 3g05s \xC2\xB7 S %91");
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
    EXPECT_EQ(lines.line1, "Oturum \xE2\x80\x94");
    EXPECT_EQ(lines.line2, "Hafta %10 \xC2\xB7 \xE2\x80\x94");
    EXPECT_EQ(lines.level1, display::Level::Dim);
}

TEST(Display, Thresholds) {
    const display::Options opt{75, 90, false};
    EXPECT_EQ(display::level_for(74.9, opt), display::Level::Normal);
    EXPECT_EQ(display::level_for(75.0, opt), display::Level::Warn);
    EXPECT_EQ(display::level_for(90.0, opt), display::Level::Critical);
}
