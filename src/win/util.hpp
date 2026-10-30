#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace cub::win {

[[nodiscard]] std::wstring to_wide(std::string_view utf8);
[[nodiscard]] std::string to_utf8(std::wstring_view wide);

/// "Win32 5: Access is denied." style text for a GetLastError() code.
[[nodiscard]] std::string error_text(unsigned long code);

[[nodiscard]] std::int64_t now_unix_ms() noexcept;
[[nodiscard]] inline std::int64_t now_unix_s() noexcept { return now_unix_ms() / 1000; }

[[nodiscard]] std::filesystem::path exe_path();
[[nodiscard]] std::filesystem::path local_app_data();
[[nodiscard]] std::filesystem::path user_profile();
[[nodiscard]] std::filesystem::path documents_dir();
/// Claude Code's config folder: %CLAUDE_CONFIG_DIR%, else %USERPROFILE%\.claude
[[nodiscard]] std::filesystem::path claude_config_dir();

/// Reads a whole file as bytes; nullopt if it cannot be opened.
[[nodiscard]] std::optional<std::string> read_file(const std::filesystem::path& p);

/// Writes `data` next to `target` and atomically swaps it in.
/// ReplaceFileW keeps the original ACL/attributes; falls back to MoveFileExW.
bool write_file_atomic(const std::filesystem::path& target, std::string_view data);

}  // namespace cub::win
