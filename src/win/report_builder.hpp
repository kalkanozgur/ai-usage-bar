// Collects the report data (CSV samples, transcripts, cloud summaries) and writes report.html.
// Runs on a short-lived background thread, only when the user opens the chart.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/usage_math.hpp"
#include "win/config.hpp"
#include "win/usage_fetcher.hpp"

namespace cub::win {

[[nodiscard]] std::filesystem::path report_path(const AppPaths& paths);
[[nodiscard]] std::filesystem::path csv_path(const AppPaths& paths, std::int64_t unix_s);

/// Deletes usage-YYYY-MM.csv files older than `keep_months` (see usage::csv_expired).
void prune_csv(const AppPaths& paths, int keep_months);

/// Builds %LOCALAPPDATA%\ClaudeUsageBar\report.html. Transcript results are cached by
/// (path, mtime, size) in breakdown-cache.jsonl so only new or changed sessions are parsed.
/// Returns false (and a short reason) on failure or when `stop` was raised.
bool build_report(const AppPaths& paths, const Config& cfg, const std::atomic<bool>& stop,
                  std::string& error);

}  // namespace cub::win
