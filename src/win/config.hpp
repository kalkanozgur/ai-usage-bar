// User settings, stored as an INI file in %LOCALAPPDATA%\ClaudeUsageBar\config.ini.
#pragma once

#include <filesystem>
#include <string>

namespace cub::win {

struct Config {
    enum class MonitorSel { Right, Left, Primary, Index };
    enum class Visibility { DesktopWindow, DesktopProcess, Always };
    enum class AuthMode { Auto, OAuth, Session };

    // [display]
    MonitorSel monitor = MonitorSel::Right;
    int monitor_index = 0;
    int gap_px = 8;
    int secondary_reserved_px = 110;
    int manual_right_px = -1;  ///< >= 0 overrides tray detection
    std::wstring font_name = L"Segoe UI";
    int font_pt = 9;
    double warn_pct = 75.0;
    double crit_pct = 90.0;
    bool color_pace = true;     ///< color by pace (true) or by warn_pct thresholds (false)
    double pace_margin = 5.0;   ///< points ahead of even spend before turning orange
    double pace_min_pct = 25.0; ///< no pace warning below this usage
    bool show_sonnet = false;
    Visibility visibility = Visibility::DesktopWindow;
    std::wstring desktop_path_hint;  ///< extra install dir substring for Claude Desktop

    // [fetch]
    int refresh_sec = 180;
    AuthMode auth = AuthMode::Auto;
    bool oauth_refresh = true;  ///< refresh expired Claude Code tokens (only while the CLI is closed)
    std::wstring org_id;        ///< cached claude.ai organization uuid

    // [report]
    bool csv_log = true;          ///< append one CSV row per successful query
    int report_sessions = 30;     ///< newest transcripts analysed for the report
    std::wstring summary_dir;     ///< *_usage.json folder; empty → Documents\Claude-Logs
    int csv_keep_months = 3;      ///< delete older usage-*.csv at startup; 0 = keep all
};

/// Loads the INI, writing a commented default file first if it does not exist.
[[nodiscard]] Config load_config(const std::filesystem::path& ini);

void save_org_id(const std::filesystem::path& ini, const std::wstring& org_id);

}  // namespace cub::win
