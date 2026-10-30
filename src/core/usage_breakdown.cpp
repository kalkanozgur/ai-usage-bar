#include "core/usage_breakdown.hpp"

#include <algorithm>
#include <array>
#include <charconv>

#include "core/time_util.hpp"

namespace cub::breakdown {
namespace {

std::optional<std::string> str_member(std::string_view doc, json::Span obj,
                                      std::string_view key) {
    const auto m = json::member(doc, obj, key);
    if (!m) {
        return std::nullopt;
    }
    return json::as_string(doc, *m);
}

double num_member(std::string_view doc, json::Span obj, std::string_view key) noexcept {
    const auto m = json::member(doc, obj, key);
    if (!m) {
        return 0.0;
    }
    const auto v = json::as_number(doc, *m);
    return v ? *v : 0.0;
}

bool true_member(std::string_view doc, json::Span obj, std::string_view key) noexcept {
    const auto m = json::member(doc, obj, key);
    return m && doc.substr(m->begin, m->size()) == "true";
}

double text_tokens(json::Span s) noexcept {
    return static_cast<double>(s.size()) / kBytesPerToken;
}

/// Estimated tokens of a content block that is not a tool_use / tool_result wrapper.
double block_tokens(std::string_view doc, json::Span block) {
    if (json::is_object(doc, block)) {
        const auto type = str_member(doc, block, "type");
        if (type && *type == "image") {
            return kImageTokens;
        }
        if (const auto text = json::member(doc, block, "text")) {
            return text_tokens(*text);
        }
    }
    return text_tokens(block);
}

/// Estimated tokens of a "content" value: a string or an array of blocks.
double content_tokens(std::string_view doc, json::Span content) {
    if (!json::is_array(doc, content)) {
        return text_tokens(content);
    }
    double t = 0.0;
    json::for_each_element(doc, content, [&](json::Span b) {
        t += block_tokens(doc, b);
        return true;
    });
    return t;
}

bool id_like(std::string_view s) noexcept {
    if (s.size() < 16) {
        return false;
    }
    return std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') ||
               c == '-';
    });
}

void append_fixed(std::string& out, double v, int precision) {
    std::array<char, 40> buf{};
    const auto r =
        std::to_chars(buf.data(), buf.data() + buf.size(), v, std::chars_format::fixed, precision);
    out.append(buf.data(), r.ptr);
}

}  // namespace

std::string group_for_tool(std::string_view n) {
    using namespace group;
    if (n.find("Claude_Browser") != std::string_view::npos) {
        return std::string(kBrowser);
    }
    if (n.starts_with("mcp__claude-in-chrome__")) {
        return std::string(kChrome);
    }
    if (n.find("computer") != std::string_view::npos) {
        return std::string(kScreen);
    }
    if (n == "WebSearch" || n == "WebFetch") {
        return std::string(kWeb);
    }
    constexpr std::array<std::string_view, 11> kFileTools = {
        "Read", "Write", "Edit", "MultiEdit", "Glob", "Grep",
        "Bash", "BashOutput", "KillShell", "NotebookEdit", "LS"};
    if (std::find(kFileTools.begin(), kFileTools.end(), n) != kFileTools.end() ||
        n.starts_with("mcp__remote-devices__device_")) {
        return std::string(kFiles);
    }
    if (n == "Skill") {
        return std::string(kSkills);
    }
    if (n == "ToolSearch") {
        return std::string(kToolLoad);
    }
    if (n == "Agent" || n == "Task") {
        return std::string(kSubagents);
    }
    if (n.starts_with("mcp__")) {
        const std::string_view rest = n.substr(5);
        const std::size_t sep = rest.find("__");
        const std::string_view server = rest.substr(0, sep);
        const std::string_view tool =
            sep == std::string_view::npos ? std::string_view{} : rest.substr(sep + 2);
        std::string out(kConnectionPrefix);
        out += (id_like(server) && !tool.empty()) ? tool : server;
        return out;
    }
    return std::string(kConversation);
}

Analyzer::Analyzer() {
    group_index(group::kInstructions);  // index 0: receives the fixed base
    group_index(group::kConversation);  // index 1
}

std::size_t Analyzer::group_index(std::string_view name) {
    for (std::size_t i = 0; i < names_.size(); ++i) {
        if (names_[i] == name) {
            return i;
        }
    }
    names_.emplace_back(name);
    content_.push_back(0.0);
    cost_.push_back(0.0);
    return names_.size() - 1;
}

void Analyzer::add_content(std::size_t g, double tokens) {
    content_[g] += tokens;
    content_total_ += tokens;
}

void Analyzer::feed_line(std::string_view line) {
    const auto root = json::root(line);
    if (!root || !json::is_object(line, *root)) {
        return;
    }
    if (meta_.session_id.empty()) {
        if (auto sid = str_member(line, *root, "sessionId")) {
            meta_.session_id = std::move(*sid);
        }
    }
    if (const auto ts = str_member(line, *root, "timestamp")) {
        if (const auto t = time::parse_iso8601(*ts)) {
            if (!meta_.started_at || *t < *meta_.started_at) {
                meta_.started_at = *t;
            }
            if (!meta_.ended_at || *t > *meta_.ended_at) {
                meta_.ended_at = *t;
            }
        }
    }
    const auto type = str_member(line, *root, "type");
    const auto msg = json::member(line, *root, "message");
    if (!type || !msg || !json::is_object(line, *msg)) {
        return;
    }
    if (*type == "assistant") {
        on_assistant(line, *msg);
    } else if (*type == "user") {
        flush();
        if (true_member(line, *root, "isCompactSummary")) {
            // Context was compacted: earlier content is gone, the summary replaces it.
            std::fill(content_.begin(), content_.end(), 0.0);
            content_total_ = 0.0;
        }
        on_user(line, *msg);
    }
}

void Analyzer::on_assistant(std::string_view line, json::Span msg) {
    auto id = str_member(line, msg, "id");
    if (!id) {
        return;
    }
    double in = 0.0;
    double cr = 0.0;
    double cw = 0.0;
    double out = 0.0;
    if (const auto usage = json::member(line, msg, "usage")) {
        in = num_member(line, *usage, "input_tokens");
        cr = num_member(line, *usage, "cache_read_input_tokens");
        cw = num_member(line, *usage, "cache_creation_input_tokens");
        out = num_member(line, *usage, "output_tokens");
    }

    if (!pending_ || pending_->id != *id) {
        flush();
        if (in + cr + cw + out <= 0.0) {
            return;  // synthetic / error entries carry no usage
        }
        Pending p;
        p.id = std::move(*id);
        p.weights = content_;
        p.content_total = content_total_;
        pending_ = std::move(p);
    }
    // One request is streamed over several lines; keep the largest figures seen.
    Pending& p = *pending_;
    p.input = std::max(p.input, in);
    p.cache_read = std::max(p.cache_read, cr);
    p.cache_write = std::max(p.cache_write, cw);
    p.output = std::max(p.output, out);

    const auto content = json::member(line, msg, "content");
    if (!content) {
        return;
    }
    if (!json::is_array(line, *content)) {
        add_content(1, text_tokens(*content));
        return;
    }
    json::for_each_element(line, *content, [&](json::Span b) {
        const auto type = str_member(line, b, "type");
        if (type && *type == "tool_use") {
            const auto name = str_member(line, b, "name");
            const std::size_t g =
                group_index(group_for_tool(name ? std::string_view(*name) : std::string_view{}));
            if (auto tid = str_member(line, b, "id")) {
                tool_groups_[std::move(*tid)] = g;
            }
            if (std::find(p.out_groups.begin(), p.out_groups.end(), g) == p.out_groups.end()) {
                p.out_groups.push_back(g);
            }
            if (const auto input = json::member(line, b, "input")) {
                add_content(g, text_tokens(*input));
            }
        } else if (type && *type == "text") {
            add_content(1, block_tokens(line, b));
        }
        // thinking blocks are billed as output but do not stay in the context
        return true;
    });
}

void Analyzer::on_user(std::string_view line, json::Span msg) {
    const auto content = json::member(line, msg, "content");
    if (!content) {
        return;
    }
    if (!json::is_array(line, *content)) {
        add_content(1, text_tokens(*content));
        return;
    }
    json::for_each_element(line, *content, [&](json::Span b) {
        const auto type = str_member(line, b, "type");
        if (type && *type == "tool_result") {
            std::size_t g = 1;
            if (const auto tid = str_member(line, b, "tool_use_id")) {
                if (const auto it = tool_groups_.find(*tid); it != tool_groups_.end()) {
                    g = it->second;
                }
            }
            if (const auto c = json::member(line, b, "content")) {
                add_content(g, content_tokens(line, *c));
            }
        } else {
            add_content(1, block_tokens(line, b));
        }
        return true;
    });
}

void Analyzer::flush() {
    if (!pending_) {
        return;
    }
    Pending p = std::move(*pending_);
    pending_.reset();

    const double context = p.input + p.cache_read + p.cache_write;
    if (!base_) {
        base_ = std::max(0.0, context - p.content_total);
    }
    const double eff_in =
        p.input + kCacheReadWeight * p.cache_read + kCacheWriteWeight * p.cache_write;
    const double eff_out = kOutputWeight * p.output;

    p.weights.resize(names_.size(), 0.0);
    p.weights[0] += *base_;
    double sum = 0.0;
    for (const double w : p.weights) {
        sum += w;
    }
    if (sum > 0.0) {
        for (std::size_t i = 0; i < p.weights.size(); ++i) {
            cost_[i] += eff_in * p.weights[i] / sum;
        }
    } else {
        cost_[0] += eff_in;
    }

    if (p.out_groups.empty()) {
        cost_[1] += eff_out;
    } else {
        const double part = eff_out / static_cast<double>(p.out_groups.size());
        for (const std::size_t g : p.out_groups) {
            cost_[g] += part;
        }
    }
    ++meta_.request_count;
    meta_.total_effective += eff_in + eff_out;
}

void Analyzer::add_subagent(const Result& sub) {
    cost_[group_index(group::kSubagents)] += sub.total_effective;
    meta_.total_effective += sub.total_effective;
}

Result Analyzer::finish() {
    flush();
    Result r = meta_;
    for (std::size_t i = 0; i < names_.size(); ++i) {
        if (cost_[i] > 0.0) {
            GroupCost g;
            g.name = names_[i];
            g.effective = cost_[i];
            g.share = r.total_effective > 0.0 ? cost_[i] / r.total_effective : 0.0;
            r.groups.push_back(std::move(g));
        }
    }
    std::stable_sort(r.groups.begin(), r.groups.end(),
                     [](const GroupCost& a, const GroupCost& b) { return a.effective > b.effective; });
    return r;
}

std::string to_summary_json(const Result& r) {
    std::string out = "{\"session\":";
    out += json::quote(r.session_id);
    out += ",\"proje\":";
    out += json::quote(r.project);
    out += ",\"baslangic\":";
    out += r.started_at ? json::quote(time::format_iso8601_utc(*r.started_at)) : "null";
    out += ",\"bitis\":";
    out += r.ended_at ? json::quote(time::format_iso8601_utc(*r.ended_at)) : "null";
    out += ",\"istek_sayisi\":";
    out += std::to_string(r.request_count);
    out += ",\"toplam_efektif\":";
    append_fixed(out, r.total_effective, 1);
    out += ",\"gruplar\":[";
    for (std::size_t i = 0; i < r.groups.size(); ++i) {
        if (i > 0) {
            out += ',';
        }
        out += "{\"ad\":";
        out += json::quote(r.groups[i].name);
        out += ",\"efektif\":";
        append_fixed(out, r.groups[i].effective, 1);
        out += ",\"pay\":";
        append_fixed(out, r.groups[i].share, 4);
        out += '}';
    }
    out += "]}";
    return out;
}

std::optional<Result> parse_summary_json(std::string_view doc) {
    const auto root = json::root(doc);
    if (!root || !json::is_object(doc, *root)) {
        return std::nullopt;
    }
    const auto groups = json::member(doc, *root, "gruplar");
    if (!groups || !json::is_array(doc, *groups)) {
        return std::nullopt;
    }
    Result r;
    r.session_id = str_member(doc, *root, "session").value_or("");
    r.project = str_member(doc, *root, "proje").value_or("");
    if (const auto s = str_member(doc, *root, "baslangic")) {
        r.started_at = time::parse_iso8601(*s);
    }
    if (const auto s = str_member(doc, *root, "bitis")) {
        r.ended_at = time::parse_iso8601(*s);
    }
    r.request_count = static_cast<std::size_t>(std::max(0.0, num_member(doc, *root, "istek_sayisi")));
    r.total_effective = num_member(doc, *root, "toplam_efektif");
    json::for_each_element(doc, *groups, [&](json::Span g) {
        GroupCost c;
        c.name = str_member(doc, g, "ad").value_or("");
        c.effective = num_member(doc, g, "efektif");
        c.share = num_member(doc, g, "pay");
        if (!c.name.empty()) {
            r.groups.push_back(std::move(c));
        }
        return true;
    });
    return r;
}

}  // namespace cub::breakdown
