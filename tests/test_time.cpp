#include <gtest/gtest.h>

#include "core/time_util.hpp"

using cub::time::days_from_civil;
using cub::time::format_remaining;
using cub::time::format_remaining_en;
using cub::time::parse_iso8601;

static_assert(days_from_civil(1970, 1, 1) == 0);
static_assert(days_from_civil(2000, 3, 1) == 11017);

TEST(Time, ParsesUtcVariants) {
    constexpr std::int64_t k = 1770415200;  // 2026-02-06T22:00:00Z
    EXPECT_EQ(parse_iso8601("2026-02-06T22:00:00Z"), k);
    EXPECT_EQ(parse_iso8601("2026-02-06T22:00:00+00:00"), k);
    EXPECT_EQ(parse_iso8601("2026-02-06T22:00:00.364238+00:00"), k);
    EXPECT_EQ(parse_iso8601("2026-02-06T22:00:00"), k);
}

TEST(Time, AppliesOffsets) {
    constexpr std::int64_t k = 1770415200;
    EXPECT_EQ(parse_iso8601("2026-02-07T01:00:00+03:00"), k);
    EXPECT_EQ(parse_iso8601("2026-02-06T17:00:00-0500"), k);
}

TEST(Time, RejectsGarbage) {
    EXPECT_FALSE(parse_iso8601(""));
    EXPECT_FALSE(parse_iso8601("2026-13-01T00:00:00Z"));
    EXPECT_FALSE(parse_iso8601("2026-02-06 22:00"));
    EXPECT_FALSE(parse_iso8601("2026-02-06T22:00:00Zjunk"));
    EXPECT_FALSE(parse_iso8601("2026-02-06T22:00:00+3"));
}

TEST(Time, FormatsRemaining) {
    EXPECT_EQ(format_remaining(-5), "0dk");
    EXPECT_EQ(format_remaining(59), "0dk");
    EXPECT_EQ(format_remaining(12 * 60), "12dk");
    EXPECT_EQ(format_remaining(2 * 3600 + 14 * 60), "2s14dk");
    EXPECT_EQ(format_remaining(3 * 86400 + 5 * 3600 + 59), "3g05s");
}

TEST(Time, FormatsRemainingEnglish) {
    EXPECT_EQ(format_remaining_en(-5), "0m");
    EXPECT_EQ(format_remaining_en(12 * 60), "12m");
    EXPECT_EQ(format_remaining_en(2 * 3600 + 14 * 60), "2h14m");
    EXPECT_EQ(format_remaining_en(3 * 86400 + 5 * 3600 + 59), "3d05h");
}
