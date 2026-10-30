cmake_minimum_required(VERSION 3.25)

project(ClaudeUsageBar
    VERSION 1.0.0
    DESCRIPTION "Claude subscription usage on the Windows taskbar"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Static CRT: the .exe runs without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(CUB_BUILD_TESTS "Build unit tests (GoogleTest)" ON)
option(CUB_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

include(cmake/CompilerWarnings.cmake)

# Source files are UTF-8 (Turkish UI strings).
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/utf-8>")

# ---------------------------------------------------------------------------
# cub_core: platform-independent logic (JSON, time, API parsing, formatting).
# Builds and is unit-tested on Linux too.
# ---------------------------------------------------------------------------
add_library(cub_core STATIC
    src/core/api.cpp
    src/core/credentials.cpp
    src/core/display.cpp
    src/core/i18n.cpp
    src/core/json_lite.cpp
    src/core/process_kind.cpp
    src/core/report_html.cpp
    src/core/time_util.cpp
    src/core/usage_breakdown.cpp
    src/core/usage_math.cpp)
target_include_directories(cub_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
cub_enable_warnings(cub_core)

# ---------------------------------------------------------------------------
# claude-usage-bar.exe: Win32 UI, WinHTTP, DPAPI.
# ---------------------------------------------------------------------------
if(WIN32)
    enable_language(RC)

    set(CUB_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
    set(CUB_MANIFEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/res/app.manifest)
    configure_file(res/version.h.in ${CUB_GENERATED_DIR}/version.h @ONLY)
    configure_file(res/app.rc.in ${CUB_GENERATED_DIR}/app.rc @ONLY)

    add_executable(claude-usage-bar WIN32
        src/win/app.cpp
        src/win/config.cpp
        src/win/http_client.cpp
        src/win/log.cpp
        src/win/main.cpp
        src/win/process_watch.cpp
        src/win/renderer.cpp
        src/win/report_builder.cpp
        src/win/secret_store.cpp
        src/win/taskbar.cpp
        src/win/usage_fetcher.cpp
        src/win/util.cpp
        ${CUB_GENERATED_DIR}/app.rc)

    target_include_directories(claude-usage-bar PRIVATE ${CUB_GENERATED_DIR})
    target_compile_definitions(claude-usage-bar PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_link_libraries(claude-usage-bar PRIVATE
        cub_core winhttp crypt32 shcore shell32 ole32 uuid advapi32 user32 gdi32)
    cub_enable_warnings(claude-usage-bar)

    if(MSVC)
        # The manifest is embedded through app.rc; stop the linker adding a second one.
        target_link_options(claude-usage-bar PRIVATE /MANIFEST:NO)
    elseif(MINGW)
        target_link_options(claude-usage-bar PRIVATE -municode -static)
    endif()

    install(TARGETS claude-usage-bar RUNTIME DESTINATION bin)
endif()

if(CUB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
