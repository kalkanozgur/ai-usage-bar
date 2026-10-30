#include <gtest/gtest.h>

#include <string>

#include "core/credentials.hpp"

namespace cred = cub::credentials;

namespace {

const std::string kFile =
    "{\n"
    "  \"claudeAiOauth\": {\n"
    "    \"accessToken\": \"sk-ant-oat01-OLD\",\n"
    "    \"refreshToken\": \"sk-ant-ort01-OLD\",\n"
    "    \"expiresAt\": 1767000000000,\n"
    "    \"scopes\": [\"user:inference\", \"user:profile\"],\n"
    "    \"subscriptionType\": \"max\"\n"
    "  },\n"
    "  \"other\": {\"accessToken\": \"must-not-change\"}\n"
    "}\n";

}  // namespace

TEST(Credentials, Parses) {
    const auto c = cred::parse(kFile);
    ASSERT_TRUE(c);
    EXPECT_EQ(c->access_token, "sk-ant-oat01-OLD");
    EXPECT_EQ(c->refresh_token, "sk-ant-ort01-OLD");
    EXPECT_EQ(c->expires_at_ms, 1767000000000);
}

TEST(Credentials, RejectsMissingToken) {
    EXPECT_FALSE(cred::parse(R"({"claudeAiOauth":{}})"));
    EXPECT_FALSE(cred::parse(R"({"somethingElse":1})"));
    EXPECT_FALSE(cred::parse("not json"));
}

TEST(Credentials, PatchReplacesOnlyTheThreeValues) {
    const cred::OAuthCredentials fresh{"sk-ant-oat01-NEW", "sk-ant-ort01-NEW", 1767999999999};
    const auto patched = cred::patch(kFile, fresh);
    ASSERT_TRUE(patched);

    std::string expected = kFile;
    auto swap = [&](const std::string& from, const std::string& to) {
        expected.replace(expected.find(from), from.size(), to);
    };
    swap("sk-ant-oat01-OLD", "sk-ant-oat01-NEW");
    swap("sk-ant-ort01-OLD", "sk-ant-ort01-NEW");
    swap("1767000000000", "1767999999999");
    EXPECT_EQ(*patched, expected);

    const auto reparsed = cred::parse(*patched);
    ASSERT_TRUE(reparsed);
    EXPECT_EQ(reparsed->access_token, fresh.access_token);
    EXPECT_EQ(reparsed->expires_at_ms, fresh.expires_at_ms);
}

TEST(Credentials, PatchFailsWhenFieldMissing) {
    EXPECT_FALSE(cred::patch(R"({"claudeAiOauth":{"accessToken":"a"}})", {"b", "c", 1}));
}

TEST(Credentials, NeedsRefresh) {
    cred::OAuthCredentials c{"a", "r", 10'000'000};
    EXPECT_FALSE(cred::needs_refresh(c, 1'000'000, 300'000));
    EXPECT_TRUE(cred::needs_refresh(c, 9'800'000, 300'000));
    c.expires_at_ms = 0;
    EXPECT_TRUE(cred::needs_refresh(c, 0, 0));
}

TEST(Credentials, RefreshRoundTrip) {
    const std::string body = cred::build_refresh_request("r\"1", "client");
    EXPECT_EQ(body, R"({"grant_type":"refresh_token","refresh_token":"r\"1","client_id":"client"})");

    const auto r = cred::parse_refresh_response(
        R"({"token_type":"Bearer","access_token":"A2","refresh_token":"R2","expires_in":28800})");
    ASSERT_TRUE(r);
    const auto applied = cred::apply_refresh({"A1", "R1", 0}, *r, 1'000);
    EXPECT_EQ(applied.access_token, "A2");
    EXPECT_EQ(applied.refresh_token, "R2");
    EXPECT_EQ(applied.expires_at_ms, 1'000 + 28'800'000);
}

TEST(Credentials, RefreshKeepsOldRefreshTokenWhenAbsent) {
    const auto r = cred::parse_refresh_response(R"({"access_token":"A2","expires_in":60})");
    ASSERT_TRUE(r);
    EXPECT_EQ(cred::apply_refresh({"A1", "R1", 0}, *r, 0).refresh_token, "R1");
    EXPECT_FALSE(cred::parse_refresh_response(R"({"error":"invalid_grant"})"));
}
