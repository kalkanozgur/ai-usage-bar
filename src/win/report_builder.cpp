#include "win/report_builder.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/json_lite.hpp"
#include "core/report_html.hpp"
#include "core/usage_breakdown.hpp"
#include "core/usage_math.hpp"
#include "win/log.hpp"
#include "win/util.hpp"

namespace cub::win {
namespace {

namespace fs = std::filesystem;

constexpr std::int64_t kDaySec = 86400;
constexpr auto kLookback = std::chrono::hours(24 * 8);  // a week plus margin

struct Transcript {
    fs::path path;
    fs::file_time_type mtime;
    std::uintmax_t size = 0;
    std::string project;
};

std::string cache_key(const Transcript& t) {
    return to_utf8(t.path.wstring()) + "|" + std::to_string(t.mtime.time_since_epoch().count()) +
           "|" + std::to_string(t.size);
}

using Cache = std::unordered_map<std::string, breakdown::Result>;

Cache load_cache(const fs::path& file) {
    Cache c;
    std::ifstream in(file, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        const auto root = json::root(line);
        if (!root) {
            continue;
        }
        const auto k = json::member(line, *root, "k");
        const auto r = json::member(line, *root, "r");
        if (!k || !r) {
            continue;
        }
        auto key = json::as_string(line, *k);
        auto res = breakdown::parse_summary_json(std::string_view(line).substr(r->begin, r->size()));
        if (key && res) {
            c.emplace(std::move(*key), std::move(*res));
        }
    }
    return c;
}

void save_cache(const fs::path& file, const Cache& c) {
    std::string out;
    for (const auto& [key, res] : c) {
        out += "{\"k\":" + json::quote(key) + ",\"r\":" + breakdown::to_summary_json(res) + "}\n";
    }
    if (!write_file_atomic(file, out)) {
        log::write("report: cache write failed");
    }
}

/// Streams one transcript; nullopt when stopped or unreadable.
std::optional<breakdown::Result> analyze_file(const fs::path& p, const std::atomic<bool>& stop,
                                              breakdown::Analyzer& a) {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string line;
    std::size_t n = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        a.feed_line(line);
        if (++n % 512 == 0 && stop) {
            return std::nullopt;
        }
    }
    return a.finish();
}

std::optional<breakdown::Result> analyze_session(const Transcript& t,
                                                 const std::atomic<bool>& stop) {
    breakdown::Analyzer a;
    // Claude Code keeps subagent transcripts in <project>/<session>/subagents/*.jsonl
    std::error_code ec;
    const fs::path sub_dir = t.path.parent_path() / t.path.stem() / L"subagents";
    for (const auto& e : fs::directory_iterator(sub_dir, ec)) {
        if (stop) {
            return std::nullopt;
        }
        if (e.is_regular_file(ec) && e.path().extension() == L".jsonl") {
            breakdown::Analyzer sa;
            if (auto sr = analyze_file(e.path(), stop, sa)) {
                a.add_subagent(*sr);
            }
        }
    }
    auto r = analyze_file(t.path, stop, a);
    if (r) {
        r->project = t.project;
    }
    return r;
}

std::vector<Transcript> list_transcripts(std::size_t max_count) {
    std::vector<Transcript> all;
    std::error_code ec;
    const fs::path root = claude_config_dir() / L"projects";
    const auto cutoff = fs::file_time_type::clock::now() - kLookback;
    for (const auto& pd : fs::directory_iterator(root, ec)) {
        if (!pd.is_directory(ec)) {
            continue;
        }
        const std::string project = to_utf8(pd.path().filename().wstring());
        for (const auto& f : fs::directory_iterator(pd.path(), ec)) {
            if (!f.is_regular_file(ec) || f.path().extension() != L".jsonl") {
                continue;
            }
            Transcript t;
            t.path = f.path();
            t.mtime = f.last_write_time(ec);
            t.size = f.file_size(ec);
            t.project = project;
            all.push_back(std::move(t));
        }
    }
    std::sort(all.begin(), all.end(),
              [](const Transcript& a, const Transcript& b) { return a.mtime > b.mtime; });
    std::size_t keep = 0;
    while (keep < all.size() && keep < max_count && (keep == 0 || all[keep].mtime >= cutoff)) {
        ++keep;  // always keep the newest one so "Last session" has data
    }
    all.resize(keep);
    return all;
}

void load_summaries(const fs::path& dir, i18n::Lang lang, std::vector<breakdown::Result>& out) {
    std::error_code ec;
    const auto cutoff = fs::file_time_type::clock::now() - kLookback;
    for (const auto& f : fs::directory_iterator(dir, ec)) {
        const std::wstring name = f.path().filename().wstring();
        if (!f.is_regular_file(ec) || name.size() < 11 ||
            name.compare(name.size() - 11, 11, L"_usage.json") != 0 ||
            f.last_write_time(ec) < cutoff) {
            continue;
        }
        const auto text = read_file(f.path());
        if (!text) {
            continue;
        }
        if (auto r = breakdown::parse_summary_json(*text)) {
            const std::string cloud = i18n::t(lang, i18n::S::Cloud);
            r->project = r->project.empty() ? cloud : r->project + " " + cloud;
            out.push_back(std::move(*r));
        }
    }
}

std::vector<usage::Sample> load_samples(const AppPaths& paths, std::int64_t now) {
    std::vector<usage::Sample> out;
    const std::int64_t from = now - 8 * kDaySec;
    std::vector<fs::path> files = {csv_path(paths, from), csv_path(paths, now)};
    if (files[0] == files[1]) {
        files.pop_back();
    }
    for (const auto& p : files) {
        std::ifstream in(p, std::ios::binary);
        std::string line;
        while (std::getline(in, line)) {
            if (auto s = usage::parse_csv_row(line); s && s->ts >= from) {
                out.push_back(*s);
            }
        }
    }
    std::sort(out.begin(), out.end(),
              [](const usage::Sample& a, const usage::Sample& b) { return a.ts < b.ts; });
    return out;
}

}  // namespace

void prune_csv(const AppPaths& paths, int keep_months) {
    if (keep_months <= 0) {
        return;
    }
    const std::int64_t now = now_unix_s();
    std::error_code ec;
    for (const auto& f : fs::directory_iterator(paths.data_dir, ec)) {
        const std::string name = to_utf8(f.path().filename().wstring());
        if (f.is_regular_file(ec) && usage::csv_expired(name, now, keep_months)) {
            fs::remove(f.path(), ec);
            log::write("csv pruned: " + name);
        }
    }
}

fs::path report_path(const AppPaths& paths) { return paths.data_dir / L"report.html"; }

fs::path csv_path(const AppPaths& paths, std::int64_t unix_s) {
    return paths.data_dir / to_wide(usage::csv_file_name(unix_s));
}

bool build_report(const AppPaths& paths, const Config& cfg, const std::atomic<bool>& stop,
                  std::string& error) {
    const auto started = std::chrono::steady_clock::now();
    report::Input in;
    in.now = now_unix_s();
    in.utc_offset_sec = utc_offset_sec();
    in.default_view = report::View::Week;
    in.lang = cfg.lang;
    in.samples = load_samples(paths, in.now);

    const fs::path cache_file = paths.data_dir / L"breakdown-cache.jsonl";
    const Cache old_cache = load_cache(cache_file);
    Cache new_cache;
    std::size_t parsed = 0;
    for (const auto& t : list_transcripts(static_cast<std::size_t>(cfg.report_sessions))) {
        if (stop) {
            error = "stopped";
            return false;
        }
        const std::string key = cache_key(t);
        if (const auto it = old_cache.find(key); it != old_cache.end()) {
            in.sessions.push_back(it->second);
        } else if (auto r = analyze_session(t, stop)) {
            ++parsed;
            in.sessions.push_back(*r);
        } else {
            continue;
        }
        new_cache.emplace(key, in.sessions.back());
    }
    save_cache(cache_file, new_cache);

    const fs::path summaries =
        cfg.summary_dir.empty() ? documents_dir() / L"Claude-Logs" : fs::path(cfg.summary_dir);
    load_summaries(summaries, cfg.lang, in.sessions);

    if (!write_file_atomic(report_path(paths), report::render_html(in))) {
        error = "report.html yazilamadi: " + error_text(GetLastError());
        return false;
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started)
                        .count();
    log::write("report: " + std::to_string(in.samples.size()) + " samples, " +
               std::to_string(in.sessions.size()) + " sessions (" + std::to_string(parsed) +
               " parsed), " + std::to_string(ms) + " ms");
    return true;
}

}  // namespace cub::win
