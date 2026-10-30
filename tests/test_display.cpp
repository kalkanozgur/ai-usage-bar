#include <gtest/gtest.h>

#include <vector>

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

TEST(Display, TooltipShowsPaceResetAndProjection) {
    const std::int64_t now = 1793383200;  // 21:00 TSİ
    cub::api::UsageSnapshot snap;
    snap.five_hour = cub::api::UsageWindow{60.0, now + 9000};          // half the window left
    snap.seven_day = cub::api::UsageWindow{10.0, now + 3 * 86400};
    std::vector<cub::usage::Sample> recent;
    for (int i = 0; i < 3; ++i) {
        cub::usage::Sample s;
        s.ts = now - 1200 + i * 600;
        s.session_pct = 40.0 + i * 10.0;  // 1 point per minute → 100 % in 40 min
        s.session_reset = now + 9000;
        recent.push_back(s);
    }
    const std::string t = cub::display::tooltip(snap, recent, now, 3 * 3600);
    EXPECT_NE(t.find("Oturum %60 \xC2\xB7 tempo 10 puan \xC3\xB6nde (beklenen %50)"),
              std::string::npos);
    EXPECT_NE(t.find("Reset 23:30 (2s30dk) \xC2\xB7 %100 tahmini 21:40"), std::string::npos);
    EXPECT_NE(t.find("Hafta %10 \xC2\xB7 tempo"), std::string::npos);
    EXPECT_NE(t.find("Reset Pzt 21:00"), std::string::npos);
    EXPECT_NE(t.find("Sol t\xC4\xB1k: grafik raporu"), std::string::npos);
}

TEST(Display, TooltipWithoutData) {
    const std::string t = cub::display::tooltip({}, {}, 0, 0);
    EXPECT_NE(t.find("Oturum \xE2\x80\x94"), std::string::npos);
    EXPECT_NE(t.find("Hafta \xE2\x80\x94"), std::string::npos);
}
