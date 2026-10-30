#include "win/app.hpp"

#include <shellapi.h>
#include <shellscalingapi.h>

#include <chrono>
#include <cwchar>
#include <exception>

#include "core/usage_math.hpp"
#include "win/log.hpp"
#include "win/report_builder.hpp"
#include "win/secret_store.hpp"
#include "win/taskbar.hpp"
#include "win/util.hpp"

namespace cub::win {
namespace {

constexpr wchar_t kWindowClass[] = L"ClaudeUsageBar.Widget";
constexpr wchar_t kAppTitle[] = L"Claude Usage Bar";
constexpr wchar_t kUsageUrl[] = L"https://claude.ai/settings/usage";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"ClaudeUsageBar";

constexpr UINT_PTR kTickTimer = 1;
constexpr UINT kTickMs = 1000;
constexpr UINT WM_APP_DATA = WM_APP + 1;
constexpr UINT WM_APP_REPORT = WM_APP + 2;  // wParam: 1 = report.html ready
constexpr std::int64_t kMinFetchGapMs = 30'000;  // Desktop reopened quickly → don't re-poll

enum MenuId : UINT {
    kIdOpen = 100,
    kIdReport,
    kIdRefresh,
    kIdPasteKey,
    kIdClearKey,
    kIdAutostart,
    kIdConfig,
    kIdLog,
    kIdExit,
};

UINT monitor_dpi(HMONITOR mon) {
    UINT x = 96;
    UINT y = 96;
    if (FAILED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &x, &y))) {
        return 96;
    }
    return y;
}

bool same(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

bool same(const TextBlock& a, const TextBlock& b) {
    return a.line1 == b.line1 && a.line2 == b.line2 && a.color1 == b.color1 &&
           a.color2 == b.color2;
}

void open_path(const std::filesystem::path& p) {
    ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::wstring trim(std::wstring s) {
    const auto ws = L" \t\r\n\"";
    const auto b = s.find_first_not_of(ws);
    if (b == std::wstring::npos) {
        return {};
    }
    const auto e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

}  // namespace

App::App(HINSTANCE instance, AppPaths paths, Config cfg)
    : instance_(instance), paths_(std::move(paths)), cfg_(std::move(cfg)),
      watch_(cfg_.desktop_path_hint) {
    update_theme();
    taskbar_created_msg_ = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = &App::wnd_proc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                            kWindowClass, kAppTitle, WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                            instance_, this);
    if (hwnd_ == nullptr) {
        log::write("CreateWindowExW failed: " + error_text(GetLastError()));
    }
    worker_ = std::thread(&App::worker_loop, this);
}

App::~App() {
    report_stop_ = true;
    if (report_thread_.joinable()) {
        report_thread_.join();  // stops at the next line batch / file
    }
    stop_worker();
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
    }
    UnregisterClassW(kWindowClass, instance_);
}

int App::run() {
    if (hwnd_ == nullptr) {
        return 1;
    }
    SetTimer(hwnd_, kTickTimer, kTickMs, nullptr);
    on_tick();
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK App::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr || self->hwnd_ == nullptr) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return self->handle(msg, wp, lp);
}

LRESULT App::handle(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == taskbar_created_msg_ && msg != 0) {  // explorer.exe restarted
        force_render_ = true;
        return 0;
    }
    switch (msg) {
        case WM_TIMER:
            if (wp == kTickTimer) {
                on_tick();
            }
            return 0;
        case WM_APP_DATA:
            force_render_ = true;
            on_tick();
            return 0;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_LBUTTONUP: open_report(); return 0;
        case WM_APP_REPORT:
            if (wp != 0) {
                open_path(report_path(paths_));
            } else {
                MessageBoxW(hwnd_, L"Grafik raporu oluşturulamadı. Ayrıntı için log dosyasına bakın.",
                            kAppTitle, MB_ICONWARNING | MB_OK);
            }
            return 0;
        case WM_RBUTTONUP: show_menu(); return 0;
        case WM_SETTINGCHANGE:
            if (lp != 0 && std::wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0) {
                update_theme();
                force_render_ = true;
            }
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED: force_render_ = true; return 0;
        case WM_DESTROY:
            KillTimer(hwnd_, kTickTimer);
            PostQuitMessage(0);
            hwnd_ = nullptr;
            return 0;
        default: return DefWindowProcW(hwnd_, msg, wp, lp);
    }
}

void App::update_theme() {
    DWORD value = 0;
    DWORD size = sizeof value;
    light_theme_ = RegGetValueW(HKEY_CURRENT_USER,
                                L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value,
                                &size) == ERROR_SUCCESS &&
                   value == 1;
}

COLORREF App::color_for(display::Level level) const {
    switch (level) {
        case display::Level::Critical: return RGB(0xE5, 0x48, 0x4D);
        case display::Level::Warn: return RGB(0xF0, 0xA0, 0x30);
        case display::Level::Dim: return light_theme_ ? RGB(0x70, 0x70, 0x70) : RGB(0x9A, 0x9A, 0x9A);
        case display::Level::Normal:
        default: return light_theme_ ? RGB(0x1A, 0x1A, 0x1A) : RGB(0xFF, 0xFF, 0xFF);
    }
}

void App::hide() {
    if (visible_) {
        ShowWindow(hwnd_, SW_HIDE);
        visible_ = false;
    }
}

void App::on_tick() {
    bool want = true;
    switch (cfg_.visibility) {
        case Config::Visibility::DesktopWindow: want = watch_.desktop_window_present(); break;
        case Config::Visibility::DesktopProcess: want = watch_.desktop_process_running(); break;
        case Config::Visibility::Always: break;
    }
    set_active(want);

    const auto place = want ? locate_taskbar(cfg_) : std::nullopt;
    if (!place || fullscreen_app_on(place->monitor_rect, hwnd_)) {
        hide();
        return;
    }

    display::Lines lines;
    {
        const std::lock_guard lock(mtx_);
        if (snapshot_) {
            const bool stale = !last_error_.empty() ||
                               now_unix_ms() - last_ok_ms_ > 3LL * cfg_.refresh_sec * 1000;
            lines = display::format(*snapshot_, now_unix_s(),
                                    {cfg_.warn_pct, cfg_.crit_pct, cfg_.show_sonnet}, stale);
        } else if (!last_error_.empty()) {
            lines = {"Claude \xE2\x9A\xA0", "hata \xC2\xB7 sa\xC4\x9F t\xC4\xB1k",
                     display::Level::Dim, display::Level::Dim};
        } else {
            lines = {"Claude", "y\xC3\xBCkleniyor\xE2\x80\xA6", display::Level::Dim,
                     display::Level::Dim};
        }
    }

    TextBlock block{to_wide(lines.line1), to_wide(lines.line2), color_for(lines.level1),
                    color_for(lines.level2)};
    const UINT dpi = monitor_dpi(place->monitor);
    const int right = place->anchor_right - cfg_.gap_px;
    const RECT& tb = place->taskbar_rect;

    const bool geometry_changed = right != shown_right_ || dpi != shown_dpi_ ||
                                  tb.top != shown_rect_.top ||
                                  (tb.bottom - tb.top) != (shown_rect_.bottom - shown_rect_.top);
    if (force_render_ || geometry_changed || !same(block, shown_)) {
        shown_rect_ = renderer_.render(hwnd_, block, right, tb.top, tb.bottom - tb.top, dpi,
                                       cfg_.font_name, cfg_.font_pt);
        shown_ = std::move(block);
        shown_dpi_ = dpi;
        shown_right_ = right;
        force_render_ = false;
    }
    if (!visible_) {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        visible_ = true;
    }
    // The taskbar is topmost too; clicking it raises it over us, so re-assert every tick.
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

std::wstring App::status_line() {
    const std::lock_guard lock(mtx_);
    if (!last_error_.empty()) {
        std::wstring e = to_wide(last_error_);
        if (e.size() > 80) {
            e = e.substr(0, 77) + L"...";
        }
        return L"Hata: " + e;
    }
    if (!snapshot_) {
        return L"Henüz veri yok";
    }
    wchar_t hhmm[16];
    std::swprintf(hhmm, 16, L"%02u:%02u", static_cast<unsigned>(last_ok_local_.wHour),
                  static_cast<unsigned>(last_ok_local_.wMinute));
    return L"Kaynak: " + to_wide(source_) + L" · son güncelleme " + hhmm;
}

void App::show_menu() {
    HMENU m = CreatePopupMenu();
    const std::wstring status = status_line();
    const bool has_key = std::filesystem::exists(paths_.session_key);

    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, status.c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (report_busy_ ? MF_GRAYED : 0), kIdReport,
                L"Grafik raporunu aç");
    AppendMenuW(m, MF_STRING, kIdOpen, L"Kullanım sayfasını aç");
    AppendMenuW(m, MF_STRING, kIdRefresh, L"Şimdi yenile");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kIdPasteKey, L"sessionKey'i panodan kaydet");
    AppendMenuW(m, MF_STRING | (has_key ? 0 : MF_GRAYED), kIdClearKey, L"Kayıtlı sessionKey'i sil");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (autostart_enabled() ? MF_CHECKED : 0), kIdAutostart,
                L"Windows ile başlat");
    AppendMenuW(m, MF_STRING, kIdConfig, L"Ayar dosyasını aç");
    AppendMenuW(m, MF_STRING, kIdLog, L"Log dosyasını aç");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kIdExit, L"Çıkış");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);  // required so the menu closes on outside click
    const UINT cmd = static_cast<UINT>(TrackPopupMenu(
        m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RIGHTALIGN, pt.x, pt.y, 0,
        hwnd_, nullptr));
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(m);

    switch (cmd) {
        case kIdOpen: ShellExecuteW(nullptr, L"open", kUsageUrl, nullptr, nullptr, SW_SHOWNORMAL); break;
        case kIdReport: open_report(); break;
        case kIdRefresh: request_refresh(); break;
        case kIdPasteKey: paste_session_key(); break;
        case kIdClearKey:
            secret::remove(paths_.session_key);
            request_refresh();
            break;
        case kIdAutostart: toggle_autostart(); break;
        case kIdConfig: open_path(paths_.config); break;
        case kIdLog:
            log::write("log opened");
            open_path(paths_.log);
            break;
        case kIdExit: DestroyWindow(hwnd_); break;
        default: break;
    }
}

void App::paste_session_key() {
    std::wstring text;
    if (OpenClipboard(hwnd_)) {
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT); h != nullptr) {
            if (const auto* p = static_cast<const wchar_t*>(GlobalLock(h)); p != nullptr) {
                text = p;
                GlobalUnlock(h);
            }
        }
        text = trim(std::move(text));
        if (text.rfind(L"sessionKey=", 0) == 0) {
            text.erase(0, 11);
        }
        if (text.rfind(L"sk-ant-", 0) == 0) {
            EmptyClipboard();  // don't leave the secret lying around
        }
        CloseClipboard();
    }
    if (text.rfind(L"sk-ant-", 0) != 0) {
        MessageBoxW(hwnd_, L"Panoda geçerli bir sessionKey yok (\"sk-ant-\" ile başlamalı).",
                    kAppTitle, MB_ICONWARNING | MB_OK);
        return;
    }
    std::string utf8 = to_utf8(text);
    SecureZeroMemory(text.data(), text.size() * sizeof(wchar_t));
    const bool ok = secret::save(paths_.session_key, utf8);
    SecureZeroMemory(utf8.data(), utf8.size());
    MessageBoxW(hwnd_,
                ok ? L"sessionKey Windows hesabına bağlı olarak şifrelendi ve kaydedildi."
                   : L"sessionKey kaydedilemedi. Ayrıntı için log dosyasına bakın.",
                kAppTitle, (ok ? MB_ICONINFORMATION : MB_ICONERROR) | MB_OK);
    if (ok) {
        request_refresh();
    } else {
        log::write("secret::save failed: " + error_text(GetLastError()));
    }
}

void App::open_report() {
    if (report_busy_.exchange(true)) {
        return;  // already building; the browser opens when it is done
    }
    if (report_thread_.joinable()) {
        report_thread_.join();  // previous run has finished (busy was false)
    }
    const HWND notify = hwnd_;
    report_thread_ = std::thread([this, notify] {
        std::string err;
        bool ok = false;
        try {
            ok = build_report(paths_, cfg_, report_stop_, err);
        } catch (const std::exception& e) {
            err = e.what();
        }
        if (!ok) {
            log::write("report failed: " + err);
        }
        report_busy_ = false;
        if (!report_stop_ && notify != nullptr) {
            PostMessageW(notify, WM_APP_REPORT, static_cast<WPARAM>(ok), 0);
        }
    });
}

bool App::autostart_enabled() const {
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, nullptr,
                        nullptr) == ERROR_SUCCESS;
}

void App::toggle_autostart() {
    if (autostart_enabled()) {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue);
        return;
    }
    const std::wstring cmd = L"\"" + exe_path().wstring() + L"\"";
    RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, REG_SZ, cmd.c_str(),
                    static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
}

// ---------------------------------------------------------------- worker

void App::set_active(bool active) {
    const std::lock_guard lock(mtx_);
    if (active == active_) {
        return;
    }
    active_ = active;
    if (active && now_unix_ms() - last_fetch_ms_ >= kMinFetchGapMs) {
        wake_ = true;  // Desktop just opened → fetch right away
    }
    cv_.notify_all();
}

void App::request_refresh() {
    {
        const std::lock_guard lock(mtx_);
        wake_ = true;
    }
    cv_.notify_all();
}

void App::worker_loop() {
    UsageFetcher fetcher(cfg_, paths_);
    const std::int64_t refresh_ms = static_cast<std::int64_t>(cfg_.refresh_sec) * 1000;
    // hwnd_ is set before this thread starts and the UI thread may null it on exit;
    // keep a private copy. Posting to a destroyed window just fails harmlessly.
    const HWND notify = hwnd_;

    std::unique_lock lock(mtx_);
    while (!stop_) {
        if (!active_ && !wake_) {
            cv_.wait(lock, [&] { return stop_ || active_ || wake_; });
            continue;
        }
        const std::int64_t now = now_unix_ms();
        const std::int64_t due = last_fetch_ms_ + refresh_ms;
        if (!wake_ && now < due) {
            cv_.wait_for(lock, std::chrono::milliseconds(due - now),
                         [&] { return stop_ || wake_ || !active_; });
            continue;
        }
        wake_ = false;
        last_fetch_ms_ = now;

        lock.unlock();
        FetchResult r = fetcher.fetch();
        if (r.snapshot && cfg_.csv_log) {
            const std::int64_t ts = now_unix_s();
            if (!usage::append_csv(csv_path(paths_, ts), usage::from_snapshot(ts, *r.snapshot))) {
                log::write("csv append failed");
            }
        }
        lock.lock();

        if (r.snapshot) {
            snapshot_ = std::move(r.snapshot);
            source_ = std::move(r.source);
            last_error_.clear();
            last_ok_ms_ = now_unix_ms();
            GetLocalTime(&last_ok_local_);
        } else {
            last_error_ = std::move(r.error);
        }
        if (notify != nullptr) {
            PostMessageW(notify, WM_APP_DATA, 0, 0);
        }
    }
    worker_done_ = true;
}

void App::stop_worker() {
    if (!worker_.joinable()) {
        return;
    }
    {
        const std::lock_guard lock(mtx_);
        stop_ = true;
    }
    cv_.notify_all();
    // A request in flight can take up to the WinHTTP timeout; don't hang the exit on it.
    for (int i = 0; i < 60 && !worker_done_; ++i) {
        Sleep(50);
    }
    if (worker_done_) {
        worker_.join();
    } else {
        log::write("worker still busy on exit; terminating process");
        ExitProcess(0);
    }
}

}  // namespace cub::win
