// UI language: English (default) or Turkish. Every user-facing string goes through t().
// Strings are UTF-8; the Win32 layer converts them with to_wide().
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace cub::i18n {

enum class Lang { En, Tr };

/// "tr" (any case) → Tr, everything else → En.
[[nodiscard]] Lang parse_lang(std::string_view s) noexcept;

enum class S {
    // taskbar
    Session,
    Week,
    Loading,
    ErrorRightClick,
    // menu + status line
    MenuOpenReport,
    MenuOpenUsagePage,
    MenuRefresh,
    MenuPasteKey,
    MenuClearKey,
    MenuAutostart,
    MenuOpenConfig,
    MenuOpenLog,
    MenuExit,
    StatusNoData,
    StatusError,       ///< "Error: "
    StatusSource,      ///< "Source: "
    StatusLastUpdate,  ///< " · last update "
    // message boxes
    MsgReportFailed,
    MsgNoKeyOnClipboard,
    MsgKeySaved,
    MsgKeySaveFailed,
    // fetch errors
    ErrOAuthNoLogin,
    ErrOAuthCredRead,
    ErrOAuthExpired,
    ErrOAuthDenied,
    ErrUnexpected,
    ErrUnreachable,
    ErrRefreshPrefix,  ///< "OAuth refresh: "
    ErrWebNoKey,
    ErrWebNoOrg,
    ErrCookieOrCloudflare,
    // report
    ReportTitle,
    TabLastSession,
    TabToday,
    TabThisWeek,
    CardSession,
    CardWeek,
    CardRate,
    NoData,
    PaceLabel,  ///< "Pace:"
    PtsAhead,
    PtsBehind,
    Expected,  ///< "expected"
    ResetLabel,
    ProjectionAt,  ///< contains "{}" for the time
    ProjectionNone,
    LastSample,
    RateNotEnough,
    RatePoint,     ///< contains "{}" for the rate
    RateFullWeek,  ///< contains "{}" for the percentage
    ChartLimits,
    ChartLimitsEmpty,
    ChartAria,
    LegendSession,
    LegendWeek,
    ChartTokens,
    ChartTokensEmpty,
    OneSession,
    ManySessions,
    Requests,
    Total,
    TableSessions,
    ThEnded,
    ThProject,
    ThRequests,
    ThEffective,
    ThLargestGroup,
    Footer,
    Cloud,  ///< "(cloud)"
    Count_
};

[[nodiscard]] const char* t(Lang lang, S key) noexcept;
/// t() with the first "{}" replaced by `arg`.
[[nodiscard]] std::string tf(Lang lang, S key, std::string_view arg);

/// Fixed-point number with the language's decimal separator ("3.04" / "3,04").
[[nodiscard]] std::string num(Lang lang, double v, int precision);
/// Percentage: "23%" (en) / "%23" (tr).
[[nodiscard]] std::string pct(Lang lang, double v, int precision = 0);
/// Countdown: "2d05h" / "2g05s".
[[nodiscard]] std::string remaining(Lang lang, std::int64_t seconds);
/// 0 = Sunday.
[[nodiscard]] const char* weekday(Lang lang, int wd) noexcept;
/// 1..12.
[[nodiscard]] const char* month(Lang lang, unsigned m) noexcept;
/// Display name for an internal token-group id ("Bağlantı: x" → "Connector: x" in English).
[[nodiscard]] std::string group_label(Lang lang, std::string_view id);

}  // namespace cub::i18n
