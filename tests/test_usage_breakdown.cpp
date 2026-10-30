#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "core/time_util.hpp"
#include "core/usage_breakdown.hpp"

namespace bd = cub::breakdown;

namespace {

bd::Result analyze_fixture() {
    std::ifstream in(CUB_FIXTURE_DIR "/session_sample.jsonl", std::ios::binary);
    EXPECT_TRUE(in.is_open());
    bd::Analyzer a;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        a.feed_line(line);
    }
    return a.finish();
}

double cost_of(const bd::Result& r, std::string_view name) {
    for (const auto& g : r.groups) {
        if (g.name == name) {
            return g.effective;
        }
    }
    return 0.0;
}

}  // namespace

TEST(Breakdown, GroupsToolsByName) {
    EXPECT_EQ(bd::group_for_tool("Read"), bd::group::kFiles);
    EXPECT_EQ(bd::group_for_tool("Bash"), bd::group::kFiles);
    EXPECT_EQ(bd::group_for_tool("mcp__remote-devices__device_stage_files"), bd::group::kFiles);
    EXPECT_EQ(bd::group_for_tool("WebFetch"), bd::group::kWeb);
    EXPECT_EQ(bd::group_for_tool("Skill"), bd::group::kSkills);
    EXPECT_EQ(bd::group_for_tool("ToolSearch"), bd::group::kToolLoad);
    EXPECT_EQ(bd::group_for_tool("Agent"), bd::group::kSubagents);
    EXPECT_EQ(bd::group_for_tool("mcp__claude-in-chrome__computer"), bd::group::kChrome);
    EXPECT_EQ(bd::group_for_tool("mcp__remote-devices__Claude_Browser__computer"),
              bd::group::kBrowser);
    EXPECT_EQ(bd::group_for_tool("mcp__remote-devices__computer_screenshot"), bd::group::kScreen);
    EXPECT_EQ(bd::group_for_tool("mcp__memory__memory_read"), "Bağlantı: memory");
    EXPECT_EQ(bd::group_for_tool("mcp__0f3c9a1b2c3d4e5f6a7b__search_issues"),
              "Bağlantı: search_issues");
    EXPECT_EQ(bd::group_for_tool("SomethingElse"), bd::group::kConversation);
}

TEST(Breakdown, CountsEachRequestOnceAndSkipsZeroUsage) {
    const auto r = analyze_fixture();
    EXPECT_EQ(r.session_id, "s-123");
    EXPECT_EQ(r.request_count, 3u);
    // m1: 10 + 2×1000 + 5×20 = 2110 · m2: 5 + 0.1×1000 + 2×200 + 5×50 = 755 · m3: 3 + 130 + 50 = 183
    EXPECT_NEAR(r.total_effective, 3048.0, 1e-9);
    EXPECT_EQ(r.started_at, cub::time::parse_iso8601("2026-10-30T18:00:00Z"));
    EXPECT_EQ(r.ended_at, cub::time::parse_iso8601("2026-10-30T18:05:00Z"));
}

TEST(Breakdown, AttributesInputAndOutputCost) {
    const auto r = analyze_fixture();
    // Content (bytes/4): user "abcdefgh" 2.5 + text "ok" 1 → conversation 3.5
    //   Read input {"p":"x"} 2.25 + result text 10.5 + image 800 → files 812.75
    //   chrome input {} 0.5 + result "done" 1.5 → 2.0
    // Base = first context 1010 − 2.5 = 1007.5
    const double base = 1007.5;
    const double w2 = base + 3.5 + 812.75;        // weights when m2 was sent
    const double w3 = base + 3.5 + 812.75 + 2.0;  // weights when m3 was sent
    const double instr = 2010.0 * base / 1010.0 + 505.0 * base / w2 + 133.0 * base / w3;
    const double conv = 2010.0 * 2.5 / 1010.0 + 505.0 * 3.5 / w2 + 133.0 * 3.5 / w3 + 50.0;
    const double files = 100.0 + 505.0 * 812.75 / w2 + 133.0 * 812.75 / w3;
    const double chrome = 250.0 + 133.0 * 2.0 / w3;

    EXPECT_NEAR(cost_of(r, bd::group::kInstructions), instr, 1e-6);
    EXPECT_NEAR(cost_of(r, bd::group::kConversation), conv, 1e-6);
    EXPECT_NEAR(cost_of(r, bd::group::kFiles), files, 1e-6);
    EXPECT_NEAR(cost_of(r, bd::group::kChrome), chrome, 1e-6);

    ASSERT_EQ(r.groups.size(), 4u);
    EXPECT_EQ(r.groups.front().name, bd::group::kInstructions);  // sorted, largest first
    double share = 0.0;
    for (const auto& g : r.groups) {
        share += g.share;
    }
    EXPECT_NEAR(share, 1.0, 1e-9);
}

TEST(Breakdown, CompactionResetsContent) {
    bd::Analyzer a;
    a.feed_line(R"({"type":"user","message":{"content":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}})");
    a.feed_line(
        R"({"type":"assistant","message":{"id":"a","usage":{"input_tokens":100,"output_tokens":0}}})");
    a.feed_line(R"({"type":"user","isCompactSummary":true,"message":{"content":"s"}})");
    a.feed_line(
        R"({"type":"assistant","message":{"id":"b","usage":{"input_tokens":100,"output_tokens":0}}})");
    const auto r = a.finish();
    // Base = 100 − 10 = 90. Request b sees base 90 + summary "s" (3 bytes → 0.75).
    const double conv = 100.0 * 10.0 / 100.0 + 100.0 * 0.75 / 90.75;
    EXPECT_NEAR(cost_of(r, bd::group::kConversation), conv, 1e-9);
}

TEST(Breakdown, BillsSubagentsAsOneGroup) {
    bd::Result sub;
    sub.total_effective = 500.0;
    bd::Analyzer a;
    a.feed_line(
        R"({"type":"assistant","message":{"id":"a","usage":{"input_tokens":100,"output_tokens":0}}})");
    a.add_subagent(sub);
    const auto r = a.finish();
    EXPECT_NEAR(r.total_effective, 600.0, 1e-9);
    EXPECT_NEAR(cost_of(r, bd::group::kSubagents), 500.0, 1e-9);
}

TEST(Breakdown, SummaryJsonRoundTrips) {
    auto r = analyze_fixture();
    r.project = "Claude Widget";
    const std::string js = bd::to_summary_json(r);
    EXPECT_NE(js.find("\"baslangic\":\"2026-10-30T18:00:00Z\""), std::string::npos);
    const auto back = bd::parse_summary_json(js);
    ASSERT_TRUE(back);
    EXPECT_EQ(back->session_id, "s-123");
    EXPECT_EQ(back->project, "Claude Widget");
    EXPECT_EQ(back->request_count, 3u);
    EXPECT_NEAR(back->total_effective, 3048.0, 0.05);
    ASSERT_EQ(back->groups.size(), r.groups.size());
    EXPECT_EQ(back->groups[0].name, r.groups[0].name);
    EXPECT_NEAR(back->groups[0].share, r.groups[0].share, 1e-4);
    EXPECT_FALSE(bd::parse_summary_json("{}"));
    EXPECT_FALSE(bd::parse_summary_json("nope"));
}
