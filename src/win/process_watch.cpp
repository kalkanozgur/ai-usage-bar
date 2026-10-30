#include "win/process_watch.hpp"

#include <tlhelp32.h>

#include <cwchar>
#include <string_view>

namespace cub::win {
namespace {

constexpr ULONGLONG kCacheTtlMs = 60'000;  // PIDs get reused; don't trust old entries forever

bool is_chromium_frame(HWND hwnd) {
    wchar_t cls[64];
    const int n = GetClassNameW(hwnd, cls, 64);
    return n > 0 && std::wstring_view(cls, static_cast<std::size_t>(n)) == L"Chrome_WidgetWin_1";
}

bool is_main_window(HWND hwnd) {
    return IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr &&
           GetWindowTextLengthW(hwnd) > 0 && is_chromium_frame(hwnd);
}

}  // namespace

std::wstring process_image_path(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h == nullptr) {
        return {};
    }
    std::wstring buf(1024, L'\0');
    DWORD len = static_cast<DWORD>(buf.size());
    const BOOL ok = QueryFullProcessImageNameW(h, 0, buf.data(), &len);
    CloseHandle(h);
    if (!ok) {
        return {};
    }
    buf.resize(len);
    return buf;
}

ProcessWatch::ProcessWatch(std::wstring desktop_path_hint) : hint_(std::move(desktop_path_hint)) {}

void ProcessWatch::expire_cache() {
    const ULONGLONG now = GetTickCount64();
    if (now - cache_time_ > kCacheTtlMs) {
        cache_.clear();
        cache_time_ = now;
    }
}

process::Kind ProcessWatch::kind_of(DWORD pid) {
    expire_cache();
    if (const auto it = cache_.find(pid); it != cache_.end()) {
        return it->second;
    }
    const auto kind = process::classify(process_image_path(pid), hint_);
    cache_.emplace(pid, kind);
    return kind;
}

bool ProcessWatch::desktop_window_present() {
    // Fast path: the window we found last time is still there.
    if (last_window_ != nullptr && IsWindow(last_window_) && is_main_window(last_window_)) {
        DWORD pid = 0;
        GetWindowThreadProcessId(last_window_, &pid);
        if (kind_of(pid) == process::Kind::ClaudeDesktop) {
            return true;
        }
    }
    last_window_ = nullptr;

    struct Ctx {
        ProcessWatch* self;
        HWND found;
    } ctx{this, nullptr};
    EnumWindows(
        [](HWND hwnd, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            if (!is_main_window(hwnd)) {
                return TRUE;
            }
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (c->self->kind_of(pid) == process::Kind::ClaudeDesktop) {
                c->found = hwnd;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    last_window_ = ctx.found;
    return ctx.found != nullptr;
}

namespace {

template <class Pred>
bool any_claude_process(Pred&& pred) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return false;
    }
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof pe;
    bool hit = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !hit; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"claude.exe") == 0) {
            hit = pred(pe.th32ProcessID);
        }
    }
    CloseHandle(snap);
    return hit;
}

}  // namespace

bool ProcessWatch::desktop_process_running() {
    return any_claude_process(
        [this](DWORD pid) { return kind_of(pid) == process::Kind::ClaudeDesktop; });
}

bool claude_cli_running(const std::wstring& hint) {
    return any_claude_process([&](DWORD pid) {
        const std::wstring path = process_image_path(pid);
        // Unreadable path (e.g. elevated process): assume CLI, so we never rotate a live token.
        return path.empty() || process::classify(path, hint) == process::Kind::ClaudeCli;
    });
}

}  // namespace cub::win
