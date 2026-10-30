#include "win/util.hpp"

#include <windows.h>
#include <shlobj.h>

#include <fstream>
#include <iterator>
#include <system_error>

namespace cub::win {

std::wstring to_wide(std::string_view s) {
    if (s.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string to_utf8(std::wstring_view s) {
    if (s.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr,
                                      0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr,
                        nullptr);
    return out;
}

std::string error_text(unsigned long code) {
    return "Win32 " + std::to_string(code) + ": " +
           std::system_category().message(static_cast<int>(code));
}

std::int64_t now_unix_ms() noexcept {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER t{};
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    constexpr std::uint64_t kEpochDiff100ns = 116444736000000000ULL;  // 1601 → 1970
    return static_cast<std::int64_t>((t.QuadPart - kEpochDiff100ns) / 10000ULL);
}

std::filesystem::path exe_path() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n < buf.size()) {
            buf.resize(n);
            return buf;
        }
        buf.resize(buf.size() * 2);
    }
}

namespace {

std::filesystem::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    std::filesystem::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw))) {
        out = raw;
    }
    CoTaskMemFree(raw);
    return out;
}

}  // namespace

std::filesystem::path local_app_data() { return known_folder(FOLDERID_LocalAppData); }
std::filesystem::path user_profile() { return known_folder(FOLDERID_Profile); }
std::filesystem::path documents_dir() { return known_folder(FOLDERID_Documents); }

std::int32_t utc_offset_sec() {
    TIME_ZONE_INFORMATION tz{};
    const DWORD id = GetTimeZoneInformation(&tz);
    LONG bias = tz.Bias;  // minutes, UTC = local + bias
    if (id == TIME_ZONE_ID_DAYLIGHT) {
        bias += tz.DaylightBias;
    } else if (id == TIME_ZONE_ID_STANDARD) {
        bias += tz.StandardBias;
    }
    return static_cast<std::int32_t>(-bias * 60);
}

std::filesystem::path claude_config_dir() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"CLAUDE_CONFIG_DIR", buf, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? std::filesystem::path(buf) : user_profile() / L".claude";
}

std::optional<std::string> read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool write_file_atomic(const std::filesystem::path& target, std::string_view data) {
    std::filesystem::path tmp = target;
    tmp += L".cub-tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out) {
            return false;
        }
    }
    if (ReplaceFileW(target.c_str(), tmp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr,
                     nullptr)) {
        return true;
    }
    if (MoveFileExW(tmp.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    DeleteFileW(tmp.c_str());
    return false;
}

}  // namespace cub::win
