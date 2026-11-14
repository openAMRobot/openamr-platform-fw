# Read-only chain SIL proof: drive emulator -> CAN frames -> #49 CANopen
# client and decoders -> #48 odometry, sensors and freshness -> status record
# (JSON lines) -> host gate of platform-sw #137 (pytest side).
#
# Integration proof, do not merge. No command path.
#
#   cmake -S tests/sil/readonly -B build/sil-readonly
#   cmake --build build/sil-readonly
#   ctest --test-dir build/sil-readonly --output-on-failure
#   OAMR_SIL_BIN=build/sil-readonly/oamr_sil_readonly \
#     OAMR_GATE_DIR=<checkout of platform-sw #137>/ros2/src/openamrobot_drivers/openamrobot_drivers \
#     python3 -m pytest -W error tests/sil/readonly
cmake_minimum_required(VERSION 3.22)
project(openamr_fw_sil_readonly C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

option(OAMR_SANITIZE "Build with address and undefined-behaviour sanitizers" ON)
set(SANITIZERS "")
if(OAMR_SANITIZE)
  set(SANITIZERS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
endif()
set(OAMR_WARNINGS -Wall -Wextra -Werror -Wconversion -Wshadow -Wstrict-prototypes -Wpedantic)
add_compile_options(-g ${SANITIZERS})
add_link_options(${SANITIZERS})

# The modules under test, compiled unchanged from the merged branches.
set(OAMR_MODULE_SOURCES
  ${REPO_ROOT}/firmware/canopen/src/co_cobid.c
  ${REPO_ROOT}/firmware/canopen/src/co_sdo.c
  ${REPO_ROOT}/firmware/canopen/src/co_nmt_emcy.c
  ${REPO_ROOT}/firmware/canopen/src/co_hb_consumer.c
  ${REPO_ROOT}/firmware/canopen/src/co_upload_sched.c
  ${REPO_ROOT}/firmware/drive_telemetry/src/zlac_telemetry.c
  ${REPO_ROOT}/firmware/telemetry/src/freshness.c
  ${REPO_ROOT}/firmware/telemetry/src/diff_odometry.c
  ${REPO_ROOT}/firmware/telemetry/src/daly_rx.c
  ${REPO_ROOT}/firmware/sensor_bridge/src/tfluna.c
  ${REPO_ROOT}/firmware/sensor_bridge/src/mb7060.c
  ${REPO_ROOT}/firmware/sensor_bridge/src/icm42688.c)
add_library(oamr_modules STATIC ${OAMR_MODULE_SOURCES})
target_include_directories(oamr_modules PUBLIC
  ${REPO_ROOT}/firmware/canopen/include
  ${REPO_ROOT}/firmware/drive_telemetry/include
  ${REPO_ROOT}/firmware/telemetry/include
  ${REPO_ROOT}/firmware/sensor_bridge/include)
target_compile_options(oamr_modules PRIVATE ${OAMR_WARNINGS})
target_link_libraries(oamr_modules PUBLIC m)

add_executable(oamr_sil_readonly
  src/main.c src/chain.c src/chain_config.c src/chain_step.c
  src/record_model.c src/record_json.c src/fake_icm_spi.c)
target_include_directories(oamr_sil_readonly PRIVATE src)
target_compile_options(oamr_sil_readonly PRIVATE ${OAMR_WARNINGS})
target_link_libraries(oamr_sil_readonly PRIVATE oamr_modules)

enable_testing()
# Smoke: an unconfigured chain refuses to start and names the missing keys.
add_test(NAME sil_unconfigured
  COMMAND sh -c "printf 'start 0\\nt 1000\\n' | $<TARGET_FILE:oamr_sil_readonly> | grep -q 'unset key'")
set(OAMR_GATE_DIR "" CACHE PATH
  "openamrobot_drivers package dir of a platform-sw PR #137 checkout (outside this repo)")
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME sil_pytest
    COMMAND ${Python3_EXECUTABLE} -m pytest -q -W error ${CMAKE_CURRENT_SOURCE_DIR}
    WORKING_DIRECTORY ${REPO_ROOT})
  set_tests_properties(sil_pytest PROPERTIES
    ENVIRONMENT "OAMR_SIL_BIN=$<TARGET_FILE:oamr_sil_readonly>;OAMR_GATE_DIR=${OAMR_GATE_DIR}")
endif()
