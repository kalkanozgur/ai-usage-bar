// Detects Claude Desktop (main window / process) and the Claude Code CLI.
#pragma once

#include <windows.h>

#include <string>
#include <unordered_map>

#include "core/process_kind.hpp"

namespace cub::win {

/// Image path → kind lookups are cached per PID. Not thread-safe; use from one thread.
class ProcessWatch {
public:
    explicit ProcessWatch(std::wstring desktop_path_hint);

    /// A visible, unowned top-level Claude Desktop window exists (minimized counts, tray-only does not).
    [[nodiscard]] bool desktop_window_present();
    [[nodiscard]] bool desktop_process_running();

private:
    process::Kind kind_of(DWORD pid);
    void expire_cache();

    std::wstring hint_;
    std::unordered_map<DWORD, process::Kind> cache_;
    ULONGLONG cache_time_ = 0;
    HWND last_window_ = nullptr;
};

/// Any Claude Code CLI process running? Stateless; safe from any thread.
[[nodiscard]] bool claude_cli_running(const std::wstring& desktop_path_hint);

/// Full image path of a process, empty if not accessible.
[[nodiscard]] std::wstring process_image_path(DWORD pid);

}  // namespace cub::win
