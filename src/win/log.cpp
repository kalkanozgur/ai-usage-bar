#include "win/log.hpp"

#include <windows.h>

#include <cstdio>
#include <mutex>
#include <system_error>

namespace cub::log {
namespace {

std::mutex g_mutex;
std::filesystem::path g_path;

}  // namespace

void init(const std::filesystem::path& file, std::uintmax_t max_bytes) {
    const std::lock_guard lock(g_mutex);
    g_path = file;
    std::error_code ec;
    if (std::filesystem::file_size(file, ec) > max_bytes && !ec) {
        std::filesystem::remove(file, ec);
    }
}

void write(std::string_view msg) noexcept {
    try {
        const std::lock_guard lock(g_mutex);
        if (g_path.empty()) {
            return;
        }
        FILE* f = nullptr;
        if (_wfopen_s(&f, g_path.c_str(), L"ab") != 0 || f == nullptr) {
            return;
        }
        SYSTEMTIME t{};
        GetLocalTime(&t);
        std::fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u  %.*s\r\n", t.wYear, t.wMonth, t.wDay,
                     t.wHour, t.wMinute, t.wSecond, static_cast<int>(msg.size()), msg.data());
        std::fclose(f);
    } catch (...) {
        // logging must never take the app down
    }
}

}  // namespace cub::log
