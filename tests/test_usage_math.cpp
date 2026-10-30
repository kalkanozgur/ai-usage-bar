#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/time_util.hpp"
#include "core/usage_math.hpp"

namespace u = cub::usage;
using cub::time::civil_from_days;
using cub::time::format_iso8601_utc;

static_assert(civil_from_days(0).year == 1970);
static_assert(civil_from_days(11017).month == 3);  // 2000-03-01

TEST(UsageMath, FormatsIsoUtc) {
    EXPECT_EQ(format_iso8601_utc(0), "1970-01-01T00:00:00Z");
    EXPECT_EQ(format_iso8601_utc(1770415200), "2026-02-06T22:00:00Z");
    EXPECT_EQ(format_iso8601_utc(-1), "1969-12-31T23:59:59Z");
}

TEST(UsageMath, CsvRoundTrip) {
    u::Sample s;
    s.ts = 1793383200;
    s.session_pct = 23.0;
    s.session_reset = 1793397600;
    s.week_pct = 41.5;
    const std::string row = u::to_csv_row(s);
    EXPECT_EQ(row, "1793383200,23.0,1793397600,41.5,");
    const auto back = u::parse_csv_row(row + "\r");
    ASSERT_TRUE(back);
    EXPECT_EQ(back->ts, s.ts);
    EXPECT_DOUBLE_EQ(*back->session_pct, 23.0);
    EXPECT_EQ(back->session_reset, s.session_reset);
    EXPECT_DOUBLE_EQ(*back->week_pct, 41.5);
    EXPECT_FALSE(back->week_reset);
}

TEST(UsageMath, CsvRejectsHeaderAndGarbage) {
    EXPECT_FALSE(u::parse_csv_row(u::csv_header()));
    EXPECT_FALSE(u::parse_csv_row(""));
    EXPECT_FALSE(u::parse_csv_row("1,2,3"));
    EXPECT_FALSE(u::parse_csv_row("1,2,3,4,5,6"));
    EXPECT_FALSE(u::parse_csv_row("1,x,,,"));
}

TEST(UsageMath, MonthlyFileName) {
    EXPECT_EQ(u::csv_file_name(1793383200), "usage-2026-10.csv");
    EXPECT_EQ(u::csv_file_name(0), "usage-1970-01.csv");
}

TEST(UsageMath, AppendWritesHeaderOnce) {
    const auto dir = std::filesystem::temp_directory_path() / "cub_usage_math_test";
    std::filesystem::create_directories(dir);
    const auto file = dir / "usage.csv";
    std::filesystem::remove(file);
    u::Sample s;
    s.ts = 100;
    ASSERT_TRUE(u::append_csv(file, s));
    s.ts = 200;
    ASSERT_TRUE(u::append_csv(file, s));
    std::ifstream in(file, std::ios::binary);
    const std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(all, std::string(u::csv_header()) + "\r\n100,,,,\r\n200,,,,\r\n");
    in.close();
    std::filesystem::remove_all(dir);
}

TEST(UsageMath, Pace) {
    const auto p = u::pace(60.0, 10'000, u::kSessionWindowSec, 1'000);  // 9000 s left of 18000
    ASSERT_TRUE(p);
    EXPECT_DOUBLE_EQ(p->expected_pct, 50.0);
    EXPECT_DOUBLE_EQ(p->delta_pct, 10.0);
    const auto late = u::pace(10.0, 0, u::kSessionWindowSec, 50);  // past reset → clamps
    ASSERT_TRUE(late);
    EXPECT_DOUBLE_EQ(late->expected_pct, 100.0);
    EXPECT_FALSE(u::pace(1.0, 0, 0, 0));
}

namespace {
u::Sample sess(std::int64_t ts, double pct, std::int64_t reset) {
    u::Sample s;
    s.ts = ts;
    s.session_pct = pct;
    s.session_reset = reset;
    return s;
}
}  // namespace

TEST(UsageMath, ProjectsTimeToFull) {
    // 10 % per 600 s → from 30 % at t=1200, 100 % is reached 4200 s later.
    const std::vector<u::Sample> v = {sess(0, 10, 18'000), sess(600, 20, 18'030),
                                      sess(1200, 30, 18'000)};
    EXPECT_EQ(u::projected_full(v, u::Window::Session), 5400);
    EXPECT_FALSE(u::projected_full(v, u::Window::Week));
}

TEST(UsageMath, ProjectionIgnoresPreviousWindowAndLateFinish) {
    // Older window (reset 1000) must not bend the fit.
    const std::vector<u::Sample> v = {sess(0, 90, 1'000), sess(2000, 10, 20'000),
                                      sess(2600, 20, 20'000), sess(3200, 30, 20'000)};
    EXPECT_EQ(u::projected_full(v, u::Window::Session), 3200 + 4200);
    // Slow burn: would only hit 100 % after the reset.
    const std::vector<u::Sample> slow = {sess(0, 1, 3'000), sess(600, 2, 3'000)};
    EXPECT_FALSE(u::projected_full(slow, u::Window::Session));
    const std::vector<u::Sample> flat = {sess(0, 5, 9'000), sess(600, 5, 9'000)};
    EXPECT_FALSE(u::projected_full(flat, u::Window::Session));
}

TEST(UsageMath, SessionToWeekRate) {
    auto s = [](std::int64_t ts, double sp, double wp) {
        u::Sample x = sess(ts, sp, 18'000);
        x.week_pct = wp;
        x.week_reset = 600'000;
        return x;
    };
    const std::vector<u::Sample> v = {s(0, 10, 1.0), s(600, 20, 2.0), s(1200, 30, 3.5)};
    const auto rate = u::session_to_week_rate(v);
    ASSERT_TRUE(rate);
    EXPECT_DOUBLE_EQ(*rate, 2.5 / 20.0);
    EXPECT_FALSE(u::session_to_week_rate(std::vector<u::Sample>{s(0, 10, 1.0)}));
}

TEST(UsageMath, CsvRetention) {
    const std::int64_t now = 1793383200;  // 2026-10-30
    EXPECT_FALSE(u::csv_expired("usage-2026-10.csv", now, 3));
    EXPECT_FALSE(u::csv_expired("usage-2026-08.csv", now, 3));
    EXPECT_TRUE(u::csv_expired("usage-2026-07.csv", now, 3));
    EXPECT_TRUE(u::csv_expired("usage-2025-12.csv", now, 3));
    EXPECT_FALSE(u::csv_expired("usage-2026-09.csv", now, 1));  // raised to 2
    EXPECT_TRUE(u::csv_expired("usage-2026-08.csv", now, 1));
    EXPECT_FALSE(u::csv_expired("usage-2020-01.csv", now, 0));  // 0 = keep all
    EXPECT_FALSE(u::csv_expired("config.ini", now, 3));
    EXPECT_FALSE(u::csv_expired("usage-2026-13.csv", now, 3));
    EXPECT_FALSE(u::csv_expired("usage-2026-07.csv.bak", now, 3));
}
