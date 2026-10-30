include(FetchContent)

FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
    FIND_PACKAGE_ARGS NAMES GTest)  # use an installed GTest (vcpkg/MSYS2) if present
set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)  # we use the static CRT
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(cub_tests
    test_api.cpp
    test_credentials.cpp
    test_display.cpp
    test_i18n.cpp
    test_json.cpp
    test_process_kind.cpp
    test_report_html.cpp
    test_time.cpp
    test_usage_breakdown.cpp
    test_usage_math.cpp)
target_link_libraries(cub_tests PRIVATE cub_core GTest::gtest_main)
target_compile_definitions(cub_tests PRIVATE
    CUB_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
cub_enable_warnings(cub_tests)

include(GoogleTest)
gtest_discover_tests(cub_tests DISCOVERY_MODE PRE_TEST)
