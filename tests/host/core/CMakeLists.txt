# Host tests for the portable firmware core, adapters and safety core.
# Same conventions as the tests/host layout of platform-fw #27: CMake, CTest,
# GoogleTest from the Ubuntu 24.04 package libgtest-dev, -Wall -Wextra -Werror,
# address and undefined-behaviour sanitizers. Standalone project so it does not
# depend on the linorobot2 checkout the Gate A PID tests need.
#
# Build: cmake -S tests/host/core -B build/host-core
# Run:   ctest --test-dir build/host-core --output-on-failure --no-tests=error
# Or:    tools/host_tests.sh
cmake_minimum_required(VERSION 3.22)
project(openamr_fw_core_host_tests C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()
find_package(GTest 1.14 REQUIRED)
include(GoogleTest)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
include(${REPO_ROOT}/firmware/sources.cmake)

option(OAMR_SANITIZE "Build with address and undefined-behaviour sanitizers" ON)
set(SANITIZERS "")
if(OAMR_SANITIZE)
  set(SANITIZERS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
endif()
add_compile_options(-Wall -Wextra -Werror -g ${SANITIZERS})
add_link_options(${SANITIZERS})

# The firmware code is C11 and compiled exactly as on the target, plus the
# stricter warnings below.
add_library(oamr_firmware STATIC ${OAMR_FIRMWARE_SOURCES} ${OAMR_APP_SOURCES})
target_include_directories(oamr_firmware PUBLIC ${OAMR_FIRMWARE_INCLUDE_DIRS})
target_compile_options(oamr_firmware PRIVATE -Wshadow -Wconversion -Wno-sign-conversion -Wstrict-prototypes)
target_link_libraries(oamr_firmware PUBLIC m)

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
# The #17 suite is the specification; it builds once a safety core exists.
if(NOT EXISTS ${OAMR_FIRMWARE_DIR}/safety_io/src/safety_core.c)
  message(WARNING "firmware/safety_io/src/safety_core.c missing: #17 suite not built")
  list(FILTER TEST_SOURCES EXCLUDE REGEX "test_safety_core")
endif()
add_executable(core_host_tests ${TEST_SOURCES})
target_include_directories(core_host_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(core_host_tests PRIVATE oamr_firmware GTest::gtest_main)
gtest_discover_tests(core_host_tests)

# Architecture rule (I8 baseline): core and adapters never include a HAL,
# register or communication-stack header.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME architecture_no_hal_in_core
           COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/tools/check_core_portability.py ${REPO_ROOT}/firmware)
endif()
