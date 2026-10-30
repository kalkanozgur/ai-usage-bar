#include <gtest/gtest.h>

#include <string>

#include "core/report_html.hpp"

namespace r = cub::report;
namespace bd = cub::breakdown;
namespace u = cub::usage;

namespace {

bool has(const std::string& s, std::string_view needle) {
    return s.find(needle) != std::string::npos;
}

bd::Result session(std::string project, std::int64_t end, double instr, double files) {
    bd::Result s;
    s.project = std::move(project);
    s.ended_at = end;
    s.request_count = 10;
    s.total_effective = instr + files;
    s.groups = {{std::string(bd::group::kInstructions), instr, instr / (instr + files)},
                {std::string(bd::group::kFiles), files, files / (instr + files)}};
    return s;
}

}  // namespace

TEST(Report, EscapesHtml) {
    EXPECT_EQ(r::escape_html("<a href=\"x\">&'"), "&lt;a href=&quot;x&quot;&gt;&amp;&#39;");
}

TEST(Report, FormatsTokens) {
    EXPECT_EQ(r::format_tokens(812), "812");
    EXPECT_EQ(r::format_tokens(8120), "8,12 K");
    EXPECT_EQ(r::format_tokens(81200), "81,2 K");
    EXPECT_EQ(r::format_tokens(3040000), "3,04 M");
    EXPECT_EQ(r::format_tokens(30400000), "30,4 M");
}

TEST(Report, MergesSessions) {
    const auto a = session("a", 100, 300, 100);
    const auto b = session("b", 200, 100, 500);
    const auto m = r::merge({&a, &b});
    EXPECT_DOUBLE_EQ(m.total_effective, 1000.0);
    EXPECT_EQ(m.request_count, 20u);
    ASSERT_EQ(m.groups.size(), 2u);
    EXPECT_EQ(m.groups[0].name, bd::group::kFiles);
    EXPECT_DOUBLE_EQ(m.groups[0].share, 0.6);
}

TEST(Report, RendersEmptyInput) {
    r::Input in;
    in.now = 1793383200;
    const std::string html = r::render_html(in);
    EXPECT_TRUE(has(html, "<!doctype html>"));
    EXPECT_TRUE(has(html, "id=\"v-week\" checked"));
    EXPECT_FALSE(has(html, "id=\"v-day\" checked"));
    EXPECT_TRUE(has(html, "limit \xC3\xB6rne\xC4\x9Fi yok"));
    EXPECT_FALSE(has(html, "<script src"));  // inline only, nothing loaded from outside
    EXPECT_FALSE(has(html, "http://"));
}

TEST(Report, RendersSeriesBreakdownAndEscapedProjects) {
    r::Input in;
    in.now = 1793383200;  // 2026-10-30T18:00:00Z
    in.utc_offset_sec = 3 * 3600;
    for (int i = 0; i < 20; ++i) {
        u::Sample s;
        s.ts = in.now - 3600 + i * 180;
        s.session_pct = 10.0 + i;
        s.session_reset = in.now + 3600;
        s.week_pct = 40.0 + i * 0.1;
        s.week_reset = in.now + 3 * 86400;
        in.samples.push_back(s);
    }
    in.sessions.push_back(session("E--Projects-<x>", in.now - 600, 700, 300));
    in.sessions.push_back(session("old", in.now - 9 * 86400, 1, 1));  // outside the week
    in.default_view = r::View::Today;

    const std::string html = r::render_html(in);
    EXPECT_TRUE(has(html, "id=\"v-day\" checked"));
    EXPECT_TRUE(has(html, "<path class=\"line\""));
    EXPECT_TRUE(has(html, "Oturum %29"));                 // direct label of the last sample
    EXPECT_TRUE(has(html, "E--Projects-&lt;x&gt;"));     // project names are escaped
    EXPECT_FALSE(has(html, "E--Projects-<x>"));
    EXPECT_TRUE(has(html, std::string(bd::group::kInstructions)));
    EXPECT_TRUE(has(html, "Tempo:"));
    EXPECT_TRUE(has(html, "class=\"xh\""));                       // hover crosshair
    EXPECT_TRUE(has(html, "data-p=\"[[" + std::to_string(in.now - 3600) + ",10.0,40.0]"));
    EXPECT_FALSE(has(html, ">old<"));                    // 9-day-old session is not listed
}
