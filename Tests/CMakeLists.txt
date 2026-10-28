# Host unit tests for the ski logic in Software/Libs/*/Ski.
#
#   cmake -S Tests -B Tests/build && cmake --build Tests/build -j && ctest --test-dir Tests/build
#
# The Ski classes are plain C++17 with no kernel, sensor or GUI dependency,
# so they build with the host compiler and need no UNA SDK.
cmake_minimum_required(VERSION 3.21)
project(una-ski-host-tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

set(APP_LIBS "${CMAKE_CURRENT_LIST_DIR}/../Software/Libs")

include(FetchContent)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        52eb8108c5bdec04579160ae17225d66034bd723  # Same pin as the UNA SDK's host tests.
)
FetchContent_MakeAvailable(googletest)

add_executable(ski-tests
    RunTracker_test.cpp
    StormWatch_test.cpp
    SkiDay_test.cpp
    SkiTypes_test.cpp
    HeartRateLog_test.cpp

    "${APP_LIBS}/Sources/Ski/RunTracker.cpp"
    "${APP_LIBS}/Sources/Ski/StormWatch.cpp"
    "${APP_LIBS}/Sources/Ski/SkiDaySim.cpp"
    "${APP_LIBS}/Sources/Ski/Format.cpp"
    "${APP_LIBS}/Sources/Ski/HeartRateLog.cpp"
    "${APP_LIBS}/Sources/SkiTypes.cpp"
)

target_include_directories(ski-tests PRIVATE "${APP_LIBS}/Header")
target_link_libraries(ski-tests PRIVATE GTest::gtest_main)

enable_testing()
include(GoogleTest)
gtest_discover_tests(ski-tests)
