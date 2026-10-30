// Locates the taskbar on the configured monitor and the x coordinate the widget
// should end at (just left of the notification area).
#pragma once

#include <windows.h>

#include <optional>

#include "win/config.hpp"

namespace cub::win {

struct Placement {
    HMONITOR monitor = nullptr;
    RECT monitor_rect{};
    RECT taskbar_rect{};
    int anchor_right = 0;  ///< widget's right edge (physical px), before gap_px
};

/// nullopt when no horizontal taskbar exists on the selected monitor.
[[nodiscard]] std::optional<Placement> locate_taskbar(const Config& cfg);

/// True when the foreground window covers the whole monitor (games, video, presentations).
[[nodiscard]] bool fullscreen_app_on(const RECT& monitor_rect, HWND self);

}  // namespace cub::win
