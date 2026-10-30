#include "win/taskbar.hpp"

#include <algorithm>
#include <string_view>
#include <vector>

namespace cub::win {
namespace {

struct MonitorEntry {
    HMONITOR handle;
    RECT rect;
    bool primary;
};

std::vector<MonitorEntry> enum_monitors() {
    std::vector<MonitorEntry> out;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR mon, HDC, LPRECT, LPARAM lp) -> BOOL {
            MONITORINFO mi{};
            mi.cbSize = sizeof mi;
            if (GetMonitorInfoW(mon, &mi)) {
                reinterpret_cast<std::vector<MonitorEntry>*>(lp)->push_back(
                    {mon, mi.rcMonitor, (mi.dwFlags & MONITORINFOF_PRIMARY) != 0});
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&out));
    std::sort(out.begin(), out.end(),
              [](const MonitorEntry& a, const MonitorEntry& b) { return a.rect.left < b.rect.left; });
    return out;
}

std::optional<MonitorEntry> pick_monitor(const Config& cfg) {
    const auto mons = enum_monitors();
    if (mons.empty()) {
        return std::nullopt;
    }
    switch (cfg.monitor) {
        case Config::MonitorSel::Left: return mons.front();
        case Config::MonitorSel::Primary: {
            const auto it = std::find_if(mons.begin(), mons.end(),
                                         [](const MonitorEntry& m) { return m.primary; });
            return it != mons.end() ? *it : mons.front();
        }
        case Config::MonitorSel::Index:
            return mons[std::min<std::size_t>(static_cast<std::size_t>(cfg.monitor_index),
                                              mons.size() - 1)];
        case Config::MonitorSel::Right:
        default: return mons.back();
    }
}

std::wstring_view class_name(HWND hwnd, wchar_t (&buf)[64]) {
    const int n = GetClassNameW(hwnd, buf, 64);
    return {buf, static_cast<std::size_t>(std::max(n, 0))};
}

HWND find_taskbar(HMONITOR mon) {
    struct Ctx {
        HMONITOR mon;
        HWND found;
    } ctx{mon, nullptr};
    EnumWindows(
        [](HWND hwnd, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            wchar_t buf[64];
            const auto cls = class_name(hwnd, buf);
            if ((cls == L"Shell_TrayWnd" || cls == L"Shell_SecondaryTrayWnd") &&
                MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) == c->mon) {
                c->found = hwnd;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

}  // namespace

std::optional<Placement> locate_taskbar(const Config& cfg) {
    const auto mon = pick_monitor(cfg);
    if (!mon) {
        return std::nullopt;
    }
    const HWND tb = find_taskbar(mon->handle);
    if (tb == nullptr) {
        return std::nullopt;
    }
    RECT r{};
    GetWindowRect(tb, &r);
    if (r.bottom - r.top <= 0 || r.bottom - r.top > r.right - r.left) {
        return std::nullopt;  // hidden or vertical taskbar: not supported
    }

    Placement p;
    p.monitor = mon->handle;
    p.monitor_rect = mon->rect;
    p.taskbar_rect = r;

    if (cfg.manual_right_px >= 0) {
        p.anchor_right = r.right - cfg.manual_right_px;
        return p;
    }
    // Primary taskbar: the notification area is a child window. Secondary taskbars have none.
    const HWND tray = FindWindowExW(tb, nullptr, L"TrayNotifyWnd", nullptr);
    RECT tr{};
    if (tray != nullptr && IsWindowVisible(tray) && GetWindowRect(tray, &tr) && tr.left > r.left &&
        tr.left < r.right) {
        p.anchor_right = tr.left;
    } else {
        p.anchor_right = r.right - cfg.secondary_reserved_px;
    }
    return p;
}

bool fullscreen_app_on(const RECT& m, HWND self) {
    const HWND fg = GetForegroundWindow();
    if (fg == nullptr || fg == self) {
        return false;
    }
    wchar_t buf[64];
    const auto cls = class_name(fg, buf);
    if (cls == L"Shell_TrayWnd" || cls == L"Shell_SecondaryTrayWnd" || cls == L"Progman" ||
        cls == L"WorkerW") {
        return false;  // the desktop itself also spans the monitor
    }
    RECT r{};
    if (!GetWindowRect(fg, &r)) {
        return false;
    }
    return r.left <= m.left && r.top <= m.top && r.right >= m.right && r.bottom >= m.bottom;
}

}  // namespace cub::win
