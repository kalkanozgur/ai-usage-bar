#include <windows.h>

#include <filesystem>
#include <system_error>

#include "win/app.hpp"
#include "win/config.hpp"
#include "win/log.hpp"
#include "win/usage_fetcher.hpp"
#include "win/util.hpp"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE /*prev*/, PWSTR /*cmd*/, int /*show*/) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\ClaudeUsageBar.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;  // already running
    }

    cub::win::AppPaths paths;
    paths.data_dir = cub::win::local_app_data() / L"ClaudeUsageBar";
    std::error_code ec;
    std::filesystem::create_directories(paths.data_dir, ec);
    paths.config = paths.data_dir / L"config.ini";
    paths.log = paths.data_dir / L"claude_usage_bar.log";
    paths.session_key = paths.data_dir / L"session.bin";

    cub::log::init(paths.log);
    cub::win::Config cfg = cub::win::load_config(paths.config);

    int rc = 0;
    {
        cub::win::App app(instance, std::move(paths), std::move(cfg));
        rc = app.run();
    }
    if (mutex != nullptr) {
        CloseHandle(mutex);
    }
    return rc;
}
