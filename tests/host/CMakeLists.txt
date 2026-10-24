# Host C/C++ tests for openamr-platform-fw (GoogleTest from the Ubuntu 24.04 package libgtest-dev).
# Build: cmake -S tests/host -B build/host -DLINOROBOT2_HARDWARE_DIR=<linorobot2_hardware checkout>
# Run:   ctest --test-dir build/host --output-on-failure --no-tests=error
cmake_minimum_required(VERSION 3.22)
project(openamr_fw_host_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LINOROBOT2_HARDWARE_DIR "" CACHE PATH "linorobot2_hardware checkout at the base pinned in NOTICE.md")
if(NOT EXISTS "${LINOROBOT2_HARDWARE_DIR}/firmware/lib/pid/pid.cpp")
  message(FATAL_ERROR "LINOROBOT2_HARDWARE_DIR must point to linorobot2_hardware (aaf9d59); "
                      "the PID tests compare the overlay with upstream")
endif()

enable_testing()
find_package(GTest 1.14 REQUIRED)
include(GoogleTest)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(OVERLAY_DIR "${REPO_ROOT}/boards/teensy_4_0/linorobot2_overlay" CACHE PATH
    "overlay under test; tests/host/mutation_check.sh points it at a mutated copy")
set(OVERLAY "${OVERLAY_DIR}")
set(UPSTREAM_PID "${LINOROBOT2_HARDWARE_DIR}/firmware/lib/pid")

set(SANITIZERS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
add_compile_options(-Wall -Wextra -Werror -g ${SANITIZERS})
add_link_options(${SANITIZERS})

# Code under test: the overlay PID (Apache-2.0, derived from linorobot2), compiled unchanged.
add_library(overlay_pid STATIC "${OVERLAY}/lib/pid/pid.cpp")
target_include_directories(overlay_pid PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/stubs" "${OVERLAY}/lib/pid")

# Reference: the upstream PID, compiled unchanged under the name UpstreamPID.
add_library(upstream_pid STATIC "${UPSTREAM_PID}/pid.cpp")
target_include_directories(upstream_pid PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/stubs" "${UPSTREAM_PID}")
target_compile_definitions(upstream_pid PRIVATE PID=UpstreamPID)

add_executable(host_tests test_pid.cpp test_lino_base_config.cpp)
target_include_directories(host_tests PRIVATE "${OVERLAY}/config")
target_compile_definitions(host_tests PRIVATE UPSTREAM_PID_HEADER="${UPSTREAM_PID}/pid.h")
target_link_libraries(host_tests PRIVATE overlay_pid upstream_pid GTest::gtest_main)
gtest_discover_tests(host_tests)
