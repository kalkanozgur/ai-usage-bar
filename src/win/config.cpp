#include "win/config.hpp"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <iterator>

namespace cub::win {
namespace {

constexpr const char* kDefaultIni =
    "; Claude Usage Bar settings. Changes are applied on restart.\r\n"
    "\r\n"
    "[display]\r\n"
    "; right | left | primary | 0,1,2... (monitor index, ordered left to right)\r\n"
    "monitor=right\r\n"
    "; gap between the text and the notification area, in physical pixels\r\n"
    "gap_px=8\r\n"
    "; space kept free on secondary taskbars (no tray there, only the clock)\r\n"
    "secondary_reserved_px=110\r\n"
    "; >= 0 overrides detection: text right edge = taskbar right edge - this value\r\n"
    "manual_right_px=-1\r\n"
    "font_name=Segoe UI\r\n"
    "font_pt=9\r\n"
    "; pace = orange when on track to hit 100% before the reset | threshold = orange at warn_pct\r\n"
    "color_mode=pace\r\n"
    "pace_margin=5\r\n"
    "pace_min_pct=25\r\n"
    "; red at crit_pct in both modes; warn_pct is used by color_mode=threshold\r\n"
    "warn_pct=75\r\n"
    "crit_pct=90\r\n"
    "show_sonnet=0\r\n"
    "; desktop_window | desktop_process | always\r\n"
    "visibility=desktop_window\r\n"
    "; optional extra Claude Desktop install folder (substring of the exe path)\r\n"
    "desktop_path_hint=\r\n"
    "\r\n"
    "[fetch]\r\n"
    "; seconds between usage queries while Claude Desktop is open (min 30)\r\n"
    "refresh_sec=180\r\n"
    "; auto | oauth | session\r\n"
    "auth=auto\r\n"
    "; 1 = refresh an expired Claude Code token (only while the Claude Code CLI is not running)\r\n"
    "oauth_refresh=1\r\n"
    "org_id=\r\n"
    "\r\n"
    "[report]\r\n"
    "; 1 = log every successful query to usage-YYYY-MM.csv (numbers only)\r\n"
    "csv_log=1\r\n"
    "; months of usage-*.csv to keep (older files are deleted at startup); 0 = keep all\r\n"
    "csv_keep_months=3\r\n"
    "; newest Claude Code transcripts analysed when the chart opens (1-200)\r\n"
    "sessions=30\r\n"
    "; folder with cloud-session summaries (*_usage.json); empty = Documents\\Claude-Logs\r\n"
    "summary_dir=\r\n";

std::wstring get_str(const std::filesystem::path& ini, const wchar_t* sec, const wchar_t* key,
                     const wchar_t* def) {
    wchar_t buf[512];
    GetPrivateProfileStringW(sec, key, def, buf, static_cast<DWORD>(std::size(buf)), ini.c_str());
    return buf;
}

int get_int(const std::filesystem::path& ini, const wchar_t* sec, const wchar_t* key, int def) {
    const std::wstring s = get_str(ini, sec, key, L"");
    if (s.empty()) {
        return def;
    }
    wchar_t* end = nullptr;
    const long v = std::wcstol(s.c_str(), &end, 10);
    return end == s.c_str() ? def : static_cast<int>(v);
}

}  // namespace

Config load_config(const std::filesystem::path& ini) {
    std::error_code ec;
    if (!std::filesystem::exists(ini, ec)) {
        std::filesystem::create_directories(ini.parent_path(), ec);
        std::ofstream(ini, std::ios::binary) << kDefaultIni;
    }

    Config c;
    const std::wstring mon = get_str(ini, L"display", L"monitor", L"right");
    if (mon == L"left") {
        c.monitor = Config::MonitorSel::Left;
    } else if (mon == L"primary") {
        c.monitor = Config::MonitorSel::Primary;
    } else if (!mon.empty() && std::iswdigit(mon[0])) {
        c.monitor = Config::MonitorSel::Index;
        c.monitor_index = std::max(0, get_int(ini, L"display", L"monitor", 0));
    }
    c.gap_px = get_int(ini, L"display", L"gap_px", c.gap_px);
    c.secondary_reserved_px =
        get_int(ini, L"display", L"secondary_reserved_px", c.secondary_reserved_px);
    c.manual_right_px = get_int(ini, L"display", L"manual_right_px", c.manual_right_px);
    c.font_name = get_str(ini, L"display", L"font_name", c.font_name.c_str());
    c.font_pt = std::clamp(get_int(ini, L"display", L"font_pt", c.font_pt), 6, 20);
    c.warn_pct = get_int(ini, L"display", L"warn_pct", 75);
    c.crit_pct = get_int(ini, L"display", L"crit_pct", 90);
    c.color_pace = get_str(ini, L"display", L"color_mode", L"pace") != L"threshold";
    c.pace_margin = std::clamp(get_int(ini, L"display", L"pace_margin", 5), 0, 50);
    c.pace_min_pct = std::clamp(get_int(ini, L"display", L"pace_min_pct", 25), 0, 100);
    c.show_sonnet = get_int(ini, L"display", L"show_sonnet", 0) != 0;
    const std::wstring vis = get_str(ini, L"display", L"visibility", L"desktop_window");
    if (vis == L"desktop_process") {
        c.visibility = Config::Visibility::DesktopProcess;
    } else if (vis == L"always") {
        c.visibility = Config::Visibility::Always;
    }
    c.desktop_path_hint = get_str(ini, L"display", L"desktop_path_hint", L"");

    c.refresh_sec = std::max(30, get_int(ini, L"fetch", L"refresh_sec", c.refresh_sec));
    const std::wstring auth = get_str(ini, L"fetch", L"auth", L"auto");
    if (auth == L"oauth") {
        c.auth = Config::AuthMode::OAuth;
    } else if (auth == L"session") {
        c.auth = Config::AuthMode::Session;
    }
    c.oauth_refresh = get_int(ini, L"fetch", L"oauth_refresh", 1) != 0;
    c.org_id = get_str(ini, L"fetch", L"org_id", L"");

    c.csv_log = get_int(ini, L"report", L"csv_log", 1) != 0;
    c.report_sessions = std::clamp(get_int(ini, L"report", L"sessions", c.report_sessions), 1, 200);
    c.summary_dir = get_str(ini, L"report", L"summary_dir", L"");
    c.csv_keep_months = std::max(0, get_int(ini, L"report", L"csv_keep_months", 3));
    return c;
}

void save_org_id(const std::filesystem::path& ini, const std::wstring& org_id) {
    WritePrivateProfileStringW(L"fetch", L"org_id", org_id.c_str(), ini.c_str());
}

}  // namespace cub::win
