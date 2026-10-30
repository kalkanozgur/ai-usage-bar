// Token breakdown of one Claude Code session transcript (<config>/projects/*/<session>.jsonl).
//
// Only numbers and tool names are read; message text is measured by size and never stored.
//
// Effective cost of one request:
//     input + 0.1 × cache_read + 2 × cache_creation + 5 × output
// Attribution:
//   - fixed base (system prompt + tool list) = first request's context − content seen so far
//   - each request's input cost is split across the base and the content groups accumulated
//     up to that request, in proportion to their (estimated) token counts
//   - output cost goes to the group of the tools that request called
// Estimates: text ≈ bytes / 4 tokens, image ≈ 800 tokens.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/json_lite.hpp"

namespace cub::breakdown {

inline constexpr double kCacheReadWeight = 0.1;
inline constexpr double kCacheWriteWeight = 2.0;
inline constexpr double kOutputWeight = 5.0;
inline constexpr double kImageTokens = 800.0;
inline constexpr double kBytesPerToken = 4.0;

namespace group {
inline constexpr std::string_view kInstructions = "Claude talimatları";
inline constexpr std::string_view kConversation = "Konuşma/diğer";
inline constexpr std::string_view kChrome = "Claude in Chrome";
inline constexpr std::string_view kScreen = "Ekran kontrolü";
inline constexpr std::string_view kBrowser = "Yerleşik tarayıcı";
inline constexpr std::string_view kWeb = "Web";
inline constexpr std::string_view kFiles = "Dosya/shell";
inline constexpr std::string_view kSkills = "Beceriler";
inline constexpr std::string_view kToolLoad = "Araç yükleme";
inline constexpr std::string_view kSubagents = "Subagent'lar";
inline constexpr std::string_view kConnectionPrefix = "Bağlantı: ";
}  // namespace group

/// Group a tool call is billed to. Other MCP tools → "Bağlantı: <server>"; servers with a
/// random-looking ID are named after the tool instead.
[[nodiscard]] std::string group_for_tool(std::string_view tool_name);

struct GroupCost {
    std::string name;
    double effective = 0.0;
    double share = 0.0;  ///< 0..1
};

struct Result {
    std::string session_id;
    std::string project;
    std::optional<std::int64_t> started_at;  ///< Unix seconds
    std::optional<std::int64_t> ended_at;
    std::size_t request_count = 0;
    double total_effective = 0.0;
    std::vector<GroupCost> groups;  ///< non-zero groups, largest first
};

/// Streaming analyzer: feed the transcript line by line, then call finish() once.
class Analyzer {
public:
    Analyzer();

    void feed_line(std::string_view line);
    /// Bills a subagent transcript's whole cost to "Subagent'lar".
    void add_subagent(const Result& sub);
    [[nodiscard]] Result finish();

private:
    struct Pending {
        std::string id;
        double input = 0.0;
        double cache_read = 0.0;
        double cache_write = 0.0;
        double output = 0.0;
        std::vector<double> weights;  ///< content per group when the request was sent
        double content_total = 0.0;
        std::vector<std::size_t> out_groups;
    };

    std::size_t group_index(std::string_view name);
    void add_content(std::size_t g, double tokens);
    void on_assistant(std::string_view line, json::Span msg);
    void on_user(std::string_view line, json::Span msg);
    void flush();

    std::vector<std::string> names_;
    std::vector<double> content_;  ///< estimated tokens currently in context, per group
    std::vector<double> cost_;     ///< effective cost billed, per group
    double content_total_ = 0.0;
    std::optional<double> base_;
    std::optional<Pending> pending_;
    std::unordered_map<std::string, std::size_t> tool_groups_;  ///< tool_use id → group
    Result meta_;
};

/// Session summary file (<log>_usage.json) used for cloud sessions without a local transcript:
/// {"session","proje","baslangic","bitis","istek_sayisi","toplam_efektif","gruplar":[{"ad","efektif","pay"}]}
[[nodiscard]] std::string to_summary_json(const Result& r);
[[nodiscard]] std::optional<Result> parse_summary_json(std::string_view json);

}  // namespace cub::breakdown
