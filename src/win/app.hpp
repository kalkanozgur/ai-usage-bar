// Owns the widget window (UI thread) and the polling worker thread.
#pragma once

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/api.hpp"
#include "core/display.hpp"
#include "core/usage_math.hpp"
#include "win/config.hpp"
#include "win/process_watch.hpp"
#include "win/renderer.hpp"
#include "win/usage_fetcher.hpp"

namespace cub::win {

class App {
public:
    App(HINSTANCE instance, AppPaths paths, Config cfg);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    /// Runs the message loop until the user picks "Exit".
    int run();

private:
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    void on_tick();
    void hide();
    void show_menu();
    void paste_session_key();
    void open_report();
    void create_tooltip();
    [[nodiscard]] std::wstring tooltip_text();
    void toggle_autostart();
    [[nodiscard]] bool autostart_enabled() const;
    void update_theme();
    [[nodiscard]] COLORREF color_for(display::Level level) const;
    [[nodiscard]] std::wstring status_line();

    // worker side
    void worker_loop();
    void set_active(bool active);
    void request_refresh();
    void stop_worker();

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    HWND tip_ = nullptr;     ///< hover tooltip (owned by hwnd_)
    std::wstring tip_text_;  ///< buffer handed to TTN_GETDISPINFO
    AppPaths paths_;
    const Config cfg_;  // read-only after construction → safe to share with the worker
    ProcessWatch watch_;
    TextRenderer renderer_;
    UINT taskbar_created_msg_ = 0;
    bool light_theme_ = false;

    // render cache (UI thread only)
    bool visible_ = false;
    bool force_render_ = true;
    TextBlock shown_{};
    RECT shown_rect_{};
    UINT shown_dpi_ = 0;
    int shown_right_ = 0;

    // shared with worker (guarded by mtx_)
    std::mutex mtx_;
    std::condition_variable cv_;
    bool stop_ = false;
    bool active_ = false;
    bool wake_ = false;
    std::int64_t last_fetch_ms_ = 0;
    std::optional<api::UsageSnapshot> snapshot_;
    std::int64_t last_ok_ms_ = 0;
    SYSTEMTIME last_ok_local_{};
    std::string source_;
    std::string last_error_;
    std::vector<usage::Sample> recent_;  ///< last few hours, for the hover projection

    std::thread worker_;
    std::atomic<bool> worker_done_{false};

    // chart report (built on demand on a short-lived thread)
    std::thread report_thread_;
    std::atomic<bool> report_busy_{false};
    std::atomic<bool> report_stop_{false};
};

}  // namespace cub::win
