# Host tests for the sensor, IMU, BMS-decode, odometry, reset-cause and
# freshness building blocks (firmware/sensor_bridge, firmware/telemetry).
#
# Plain C11 tests with a tiny header-only harness (test_harness.h), so the
# project needs only a C compiler and CMake. Warnings as in the fw2 sprint
# rules: -Wall -Wextra -Werror -Wconversion; ASan and UBSan on by default.
#
#   cmake -S tests/host/sensors -B build/host-sensors
#   cmake --build build/host-sensors
#   ctest --test-dir build/host-sensors --output-on-failure --no-tests=error
#
# Cross-compile check (object library only, no link):
#   cmake -S tests/host/sensors -B build/m7-sensors \
#     -DCMAKE_TOOLCHAIN_FILE=tests/host/sensors/arm-none-eabi-cortex-m7.cmake
#   cmake --build build/m7-sensors
cmake_minimum_required(VERSION 3.22)
project(openamr_fw_sensors C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
file(GLOB OAMR_SENSOR_SOURCES CONFIGURE_DEPENDS
  ${REPO_ROOT}/firmware/sensor_bridge/src/*.c
  ${REPO_ROOT}/firmware/telemetry/src/*.c)
set(OAMR_SENSOR_INCLUDES
  ${REPO_ROOT}/firmware/sensor_bridge/include
  ${REPO_ROOT}/firmware/telemetry/include)
set(OAMR_WARNINGS -Wall -Wextra -Werror -Wconversion -Wshadow -Wstrict-prototypes -Wpedantic)

if(CMAKE_CROSSCOMPILING)
  # Target build: object library only, sizes reported by arm-none-eabi-size.
  add_library(oamr_sensors_obj OBJECT ${OAMR_SENSOR_SOURCES})
  target_include_directories(oamr_sensors_obj PRIVATE ${OAMR_SENSOR_INCLUDES})
  target_compile_options(oamr_sensors_obj PRIVATE ${OAMR_WARNINGS} -Os -ffunction-sections -fdata-sections)
  return()
endif()

enable_testing()
option(OAMR_SANITIZE "Build with address and undefined-behaviour sanitizers" ON)
set(SANITIZERS "")
if(OAMR_SANITIZE)
  set(SANITIZERS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
endif()
add_compile_options(-g ${SANITIZERS})
add_link_options(${SANITIZERS})

add_library(oamr_sensors STATIC ${OAMR_SENSOR_SOURCES})
target_include_directories(oamr_sensors PUBLIC ${OAMR_SENSOR_INCLUDES})
target_compile_options(oamr_sensors PRIVATE ${OAMR_WARNINGS})
target_link_libraries(oamr_sensors PUBLIC m)

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.c)
foreach(src ${TEST_SOURCES})
  get_filename_component(name ${src} NAME_WE)
  add_executable(${name} ${src})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(${name} PRIVATE ${OAMR_WARNINGS} -Wno-float-equal)
  target_link_libraries(${name} PRIVATE oamr_sensors)
  add_test(NAME ${name} COMMAND ${name})
endforeach()
