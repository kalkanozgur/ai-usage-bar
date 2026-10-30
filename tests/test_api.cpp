#include <gtest/gtest.h>

#include "core/api.hpp"

using cub::api::parse_usage;
using cub::api::pick_chat_org;

TEST(Api, ParsesOAuthUsage) {
    const auto snap = parse_usage(R"({
        "five_hour": {"utilization": 35.0, "resets_at": "2026-02-06T22:00:00+00:00"},
        "seven_day": {"utilization": 14.0, "resets_at": "2026-02-12T20:00:00+00:00"},
        "seven_day_sonnet": {"utilization": 39.0, "resets_at": "2026-02-09T14:00:00+00:00"},
        "seven_day_opus": null,
        "seven_day_oauth_apps": null,
        "extra_usage": {"is_enabled": true, "monthly_limit": 100000, "used_credits": 0.0, "utilization": null}
    })");
    ASSERT_TRUE(snap);
    ASSERT_TRUE(snap->five_hour);
    EXPECT_DOUBLE_EQ(snap->five_hour->utilization, 35.0);
    EXPECT_EQ(snap->five_hour->resets_at, 1770415200);
    ASSERT_TRUE(snap->seven_day);
    EXPECT_DOUBLE_EQ(snap->seven_day->utilization, 14.0);
    ASSERT_TRUE(snap->seven_day_sonnet);
    EXPECT_FALSE(snap->seven_day_opus);
}

TEST(Api, NullUtilizationIsZero) {
    const auto snap = parse_usage(R"({"five_hour":{"utilization":null,"resets_at":null}})");
    ASSERT_TRUE(snap);
    ASSERT_TRUE(snap->five_hour);
    EXPECT_DOUBLE_EQ(snap->five_hour->utilization, 0.0);
    EXPECT_FALSE(snap->five_hour->resets_at);
    EXPECT_FALSE(snap->seven_day);
}

TEST(Api, RejectsErrorPayloads) {
    EXPECT_FALSE(parse_usage(R"({"type":"error","error":{"type":"authentication_error"}})"));
    EXPECT_FALSE(parse_usage("<html>Just a moment...</html>"));
    EXPECT_FALSE(parse_usage(""));
}

TEST(Api, PicksChatOrganization) {
    const auto org = pick_chat_org(R"([
        {"uuid":"api-org","capabilities":["api"]},
        {"uuid":"chat-org","capabilities":["api","chat"]}
    ])");
    EXPECT_EQ(org, "chat-org");
}

TEST(Api, FallsBackToFirstOrganization) {
    EXPECT_EQ(pick_chat_org(R"([{"uuid":"only"}])"), "only");
    EXPECT_FALSE(pick_chat_org("[]"));
    EXPECT_FALSE(pick_chat_org(R"({"uuid":"x"})"));
}
