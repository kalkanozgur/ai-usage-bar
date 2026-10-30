// Classifies an executable path as Claude Desktop, Claude Code CLI or something else.
// Both products ship a "claude.exe", so the install location is what tells them apart.
#pragma once

#include <string_view>

namespace cub::process {

enum class Kind { Other, ClaudeDesktop, ClaudeCli };

/// `path` is a full Win32 image path, e.g.
///   C:\Users\me\AppData\Local\AnthropicClaude\app-1.2.3\claude.exe              → Desktop
///   C:\Program Files\WindowsApps\Claude_1.2.3.0_x64__pzs8sxrjxfjjc\app\claude.exe → Desktop
///   C:\Users\me\.local\bin\claude.exe                                            → CLI
/// `extra_desktop_hint` (case-insensitive substring) lets the user add a custom install dir.
[[nodiscard]] Kind classify(std::wstring_view path, std::wstring_view extra_desktop_hint = {});

}  // namespace cub::process
