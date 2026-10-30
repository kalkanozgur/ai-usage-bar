#include "core/i18n.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <utility>

#include "core/time_util.hpp"
#include "core/usage_breakdown.hpp"

namespace cub::i18n {
namespace {

struct Entry {
    S key;
    const char* en;
    const char* tr;
};

// Order must match enum S (checked below).
constexpr std::array<Entry, static_cast<std::size_t>(S::Count_)> kTable = {{
    {S::Session, "Session", "Oturum"},
    {S::Week, "Week", "Hafta"},
    {S::Loading, "loading…", "yükleniyor…"},
    {S::ErrorRightClick, "error · right-click", "hata · sağ tık"},

    {S::MenuOpenReport, "Open usage report", "Grafik raporunu aç"},
    {S::MenuOpenUsagePage, "Open usage page (claude.ai)", "Kullanım sayfasını aç (claude.ai)"},
    {S::MenuRefresh, "Refresh now", "Şimdi yenile"},
    {S::MenuPasteKey, "Save sessionKey from clipboard", "sessionKey'i panodan kaydet"},
    {S::MenuClearKey, "Delete saved sessionKey", "Kayıtlı sessionKey'i sil"},
    {S::MenuAutostart, "Start with Windows", "Windows ile başlat"},
    {S::MenuOpenConfig, "Open settings file", "Ayar dosyasını aç"},
    {S::MenuOpenLog, "Open log file", "Log dosyasını aç"},
    {S::MenuExit, "Exit", "Çıkış"},
    {S::StatusNoData, "No data yet", "Henüz veri yok"},
    {S::StatusError, "Error: ", "Hata: "},
    {S::StatusSource, "Source: ", "Kaynak: "},
    {S::StatusLastUpdate, " · last update ", " · son güncelleme "},

    {S::MsgReportFailed, "Could not build the usage report. See the log file for details.",
     "Grafik raporu oluşturulamadı. Ayrıntı için log dosyasına bakın."},
    {S::MsgNoKeyOnClipboard, "No valid sessionKey on the clipboard (it must start with \"sk-ant-\").",
     "Panoda geçerli bir sessionKey yok (\"sk-ant-\" ile başlamalı)."},
    {S::MsgKeySaved, "sessionKey encrypted for this Windows account and saved.",
     "sessionKey Windows hesabına bağlı olarak şifrelendi ve kaydedildi."},
    {S::MsgKeySaveFailed, "Could not save the sessionKey. See the log file for details.",
     "sessionKey kaydedilemedi. Ayrıntı için log dosyasına bakın."},

    {S::ErrOAuthNoLogin, "OAuth: not signed in to Claude Code", "OAuth: Claude Code girişi yok"},
    {S::ErrOAuthCredRead, "OAuth: cannot read credentials file", "OAuth: kimlik dosyası okunamadı"},
    {S::ErrOAuthExpired, "OAuth: token expired", "OAuth: token süresi dolmuş"},
    {S::ErrOAuthDenied, "OAuth: access denied", "OAuth: yetki reddedildi"},
    {S::ErrUnexpected, "unexpected response", "beklenmeyen yanıt"},
    {S::ErrUnreachable, "unreachable", "erişilemedi"},
    {S::ErrRefreshPrefix, "OAuth refresh: ", "OAuth yenileme: "},
    {S::ErrWebNoKey, "Web: no sessionKey saved", "Web: sessionKey kayıtlı değil"},
    {S::ErrWebNoOrg, "Web: organization not found", "Web: organizasyon bulunamadı"},
    {S::ErrCookieOrCloudflare, " (invalid cookie or Cloudflare)", " (çerez geçersiz veya Cloudflare)"},

    {S::ReportTitle, "Claude usage report", "Claude kullanım raporu"},
    {S::TabLastSession, "Last session", "Son oturum"},
    {S::TabToday, "Today", "Bugün"},
    {S::TabThisWeek, "This week", "Bu hafta"},
    {S::CardSession, "Session (5 h)", "Oturum (5 saat)"},
    {S::CardWeek, "Week (7 days)", "Hafta (7 gün)"},
    {S::CardRate, "Session → week rate", "Oturum → hafta kuru"},
    {S::NoData, "No data", "Veri yok"},
    {S::PaceLabel, "Pace:", "Tempo:"},
    {S::PtsAhead, "pts ahead", "puan önde"},
    {S::PtsBehind, "pts behind", "puan geride"},
    {S::Expected, "expected", "beklenen"},
    {S::ResetLabel, "Reset:", "Reset:"},
    {S::ProjectionAt, "Projection: 100% at <b>{}</b>", "Tahmin: <b>{}</b>'te %100"},
    {S::ProjectionNone, "Projection: won't hit 100% before reset",
     "Tahmin: reset öncesi dolmuyor"},
    {S::LastSample, "Last sample", "Son örnek"},
    {S::RateNotEnough, "Not enough samples (needs at least 1 point of session growth)",
     "Yeterli örnek yok (en az 1 oturum puanı artışı)"},
    {S::RatePoint, "1 session point ≈ {} week points", "1 oturum puanı ≈ {} hafta puanı"},
    {S::RateFullWeek, "A full session (100%) ≈ {} of the week",
     "Tam bir oturum (%100) ≈ haftanın {}'i"},
    {S::ChartLimits, "Limit usage (%)", "Limit kullanımı (%)"},
    {S::ChartLimitsEmpty,
     "No limit samples in this range yet. The widget records one on every successful query.",
     "Bu aralıkta limit örneği yok. Widget her başarılı sorguda bir örnek kaydeder."},
    {S::ChartAria, "Session and week usage", "Oturum ve hafta yüzdesi"},
    {S::LegendSession, "Session (5 h)", "Oturum (5 saat)"},
    {S::LegendWeek, "Week (7 days)", "Hafta (7 gün)"},
    {S::ChartTokens, "Token breakdown (effective)", "Token dağılımı (efektif)"},
    {S::ChartTokensEmpty, "No session transcripts or summaries in this range.",
     "Bu aralıkta oturum dökümü ya da özeti yok."},
    {S::OneSession, " session · ", " oturum · "},
    {S::ManySessions, " sessions · ", " oturum · "},
    {S::Requests, " requests · ", " istek · "},
    {S::Total, "total ", "toplam "},
    {S::TableSessions, "Sessions", "Oturumlar"},
    {S::ThEnded, "Ended", "Bitiş"},
    {S::ThProject, "Project", "Proje"},
    {S::ThRequests, "Requests", "İstek"},
    {S::ThEffective, "Effective", "Efektif"},
    {S::ThLargestGroup, "Largest group", "En büyük grup"},
    {S::Footer,
     "Effective = input + 0.1×cache read + 2×cache write + 5×output. Only numbers and tool "
     "names are read; message text is never stored.",
     "Efektif = girdi + 0,1×önbellek okuma + 2×önbellek yazma + 5×çıktı. Yalnız sayılar ve "
     "araç adları okunur; mesaj metni saklanmaz."},
    {S::Cloud, "(cloud)", "(bulut)"},
}};

constexpr bool table_in_order() {
    for (std::size_t i = 0; i < kTable.size(); ++i) {
        if (static_cast<std::size_t>(kTable[i].key) != i) {
            return false;
        }
    }
    return true;
}
static_assert(table_in_order(), "i18n table order must match enum S");

}  // namespace

Lang parse_lang(std::string_view s) noexcept {
    return s.size() == 2 && (s[0] == 't' || s[0] == 'T') && (s[1] == 'r' || s[1] == 'R')
               ? Lang::Tr
               : Lang::En;
}

const char* t(Lang lang, S key) noexcept {
    const auto i = static_cast<std::size_t>(key);
    if (i >= kTable.size()) {
        return "";
    }
    return lang == Lang::Tr ? kTable[i].tr : kTable[i].en;
}

std::string tf(Lang lang, S key, std::string_view arg) {
    std::string s = t(lang, key);
    if (const auto p = s.find("{}"); p != std::string::npos) {
        s.replace(p, 2, arg);
    }
    return s;
}

std::string num(Lang lang, double v, int precision) {
    std::array<char, 40> buf{};
    const auto r =
        std::to_chars(buf.data(), buf.data() + buf.size(), v, std::chars_format::fixed, precision);
    std::string s(buf.data(), r.ptr);
    if (lang == Lang::Tr) {
        std::replace(s.begin(), s.end(), '.', ',');
    }
    return s;
}

std::string pct(Lang lang, double v, int precision) {
    return lang == Lang::Tr ? "%" + num(lang, v, precision) : num(lang, v, precision) + "%";
}

std::string remaining(Lang lang, std::int64_t seconds) {
    return lang == Lang::Tr ? time::format_remaining(seconds) : time::format_remaining_en(seconds);
}

const char* weekday(Lang lang, int wd) noexcept {
    static constexpr std::array<const char*, 7> kEn = {"Sun", "Mon", "Tue", "Wed",
                                                       "Thu", "Fri", "Sat"};
    static constexpr std::array<const char*, 7> kTr = {"Paz", "Pzt", "Sal", "Çar",
                                                       "Per", "Cum", "Cmt"};
    const auto i = static_cast<std::size_t>(((wd % 7) + 7) % 7);
    return lang == Lang::Tr ? kTr[i] : kEn[i];
}

const char* month(Lang lang, unsigned m) noexcept {
    static constexpr std::array<const char*, 12> kEn = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static constexpr std::array<const char*, 12> kTr = {"Oca", "Şub", "Mar", "Nis", "May", "Haz",
                                                        "Tem", "Ağu", "Eyl", "Eki", "Kas", "Ara"};
    const std::size_t i = (m + 11) % 12;
    return lang == Lang::Tr ? kTr[i] : kEn[i];
}

std::string group_label(Lang lang, std::string_view id) {
    if (lang == Lang::Tr) {
        return std::string(id);  // internal ids are Turkish
    }
    namespace g = breakdown::group;
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 10> kLabels = {{
        {g::kInstructions, "Claude instructions"},
        {g::kConversation, "Conversation/other"},
        {g::kChrome, "Claude in Chrome"},
        {g::kScreen, "Computer use"},
        {g::kBrowser, "Built-in browser"},
        {g::kWeb, "Web"},
        {g::kFiles, "Files/shell"},
        {g::kSkills, "Skills"},
        {g::kToolLoad, "Tool loading"},
        {g::kSubagents, "Subagents"},
    }};
    for (const auto& [key, label] : kLabels) {
        if (id == key) {
            return std::string(label);
        }
    }
    if (id.starts_with(g::kConnectionPrefix)) {
        return "Connector: " + std::string(id.substr(g::kConnectionPrefix.size()));
    }
    return std::string(id);
}

}  // namespace cub::i18n
