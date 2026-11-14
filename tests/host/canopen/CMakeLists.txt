# Host tests for firmware/canopen and firmware/drive_telemetry.
#
# Pure C11, no external test framework, warnings as errors, AddressSanitizer
# and UndefinedBehaviorSanitizer on by default.
#
#   cmake -S tests/host/canopen -B build/host-canopen
#   cmake --build build/host-canopen
#   ctest --test-dir build/host-canopen --output-on-failure --no-tests=error
#
# Cross-compile check for the base controller (object library, no link):
#   tests/host/canopen/cross_compile_m7.sh
cmake_minimum_required(VERSION 3.22)
project(openamr_fw_canopen_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

option(OAMR_SANITIZE "Build with address and undefined-behaviour sanitizers" ON)
set(SANITIZERS "")
if(OAMR_SANITIZE)
  set(SANITIZERS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
endif()
add_compile_options(-Wall -Wextra -Werror -Wconversion -Wshadow -Wstrict-prototypes -g ${SANITIZERS})
add_link_options(${SANITIZERS})

set(OAMR_CANOPEN_SOURCES
  ${REPO_ROOT}/firmware/canopen/src/co_cobid.c
  ${REPO_ROOT}/firmware/canopen/src/co_sdo.c
  ${REPO_ROOT}/firmware/canopen/src/co_nmt_emcy.c
  ${REPO_ROOT}/firmware/canopen/src/co_hb_consumer.c
  ${REPO_ROOT}/firmware/canopen/src/co_upload_sched.c
  ${REPO_ROOT}/firmware/drive_telemetry/src/zlac_telemetry.c
  ${REPO_ROOT}/firmware/drive_telemetry/src/config_readback.c
)

add_library(oamr_canopen STATIC ${OAMR_CANOPEN_SOURCES})
target_include_directories(oamr_canopen PUBLIC
  ${REPO_ROOT}/firmware/canopen/include
  ${REPO_ROOT}/firmware/drive_telemetry/include)
target_compile_options(oamr_canopen PRIVATE -Wpedantic)

add_library(tharness STATIC tharness.c)
target_link_libraries(tharness PUBLIC oamr_canopen)

foreach(t test_codec test_hb_sched test_telemetry)
  add_executable(${t} ${t}.c)
  target_link_libraries(${t} PRIVATE tharness)
  add_test(NAME ${t} COMMAND ${t})
endforeach()

# Guard: the read-only package must not reference restricted drive objects in
# its production sources, and must not call the download encoder.
add_test(NAME read_only_guard
  COMMAND ${CMAKE_COMMAND} -DREPO_ROOT=${REPO_ROOT} -P ${CMAKE_CURRENT_SOURCE_DIR}/read_only_guard.cmake)
