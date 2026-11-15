# SPDX-License-Identifier: MIT
#
# Telemetry image (proposal): a second firmware target next to
# boards/stm32/OpenAMRobot. Read-only: it reads CAN1 and the sensor UARTs and
# prints status lines on USART3. It reuses the CubeMX-generated HAL
# configuration, MSP, start-up file, system file and linker script of
# ../OpenAMRobot unchanged; nothing in that directory is edited or copied.
#
#   cmake --preset Debug   && cmake --build --preset Debug
#   cmake --preset Release && cmake --build --preset Release
#   cmake --preset RenodeTest && cmake --build --preset RenodeTest   # unapproved test values
#   cmake --preset TelemetryUrosSerialSimBench && cmake --build --preset TelemetryUrosSerialSimBench
#        # micro-ROS over USART3: simulation and bench only (README "micro-ROS")
cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug")
endif()
set(CMAKE_PROJECT_NAME telemetry_image)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(${CMAKE_PROJECT_NAME})
enable_language(C ASM)

option(TLM_TEST_PARAMS "Activate the unapproved test values of tlm_params.h (Renode only)" OFF)
option(TLM_UROS "micro-ROS client over the serial transport (simulation and bench only)" OFF)
set(TLM_FREERTOS_TAG "V11.1.0" CACHE STRING "FreeRTOS-Kernel release tag fetched at configure time")
set(TLM_FREERTOS_SHA "dbf70559b27d39c1fdb68dfb9a32140b6a6777a0" CACHE STRING
    "Commit the tag must resolve to; configure fails on a mismatch")

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(CUBEMX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../OpenAMRobot")

add_executable(${CMAKE_PROJECT_NAME})

# ---- Generated HAL and start-up code of ../OpenAMRobot, unchanged ----------
# The generated CMake file adds the HAL driver library and the generated
# application sources to ${CMAKE_PROJECT_NAME}. Two of those sources are
# replaced by wrappers of this directory that include them verbatim:
#   main.c           -> src/cubemx_main_reuse.c (generated main() renamed, not called)
#   stm32h7xx_it.c   -> src/cubemx_it_reuse.c   (SVC, PendSV, SysTick go to FreeRTOS)
add_subdirectory(${CUBEMX_DIR}/cmake/stm32cubemx ${CMAKE_BINARY_DIR}/stm32cubemx)
get_target_property(_srcs ${CMAKE_PROJECT_NAME} SOURCES)
list(FILTER _srcs EXCLUDE REGEX "Core/Src/(main|stm32h7xx_it)\\.c$")
set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY SOURCES ${_srcs})
set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
  "${CUBEMX_DIR}/STM32H723xG_flash.ld")

# ---- FreeRTOS kernel: fetched at a pinned release tag, not vendored --------
include(FetchContent)
FetchContent_Declare(freertos_kernel
  GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
  GIT_TAG ${TLM_FREERTOS_TAG}
  GIT_SHALLOW TRUE)
FetchContent_GetProperties(freertos_kernel)
if(NOT freertos_kernel_POPULATED)
  FetchContent_Populate(freertos_kernel)   # sources only; the kernel's own CMake is not used
endif()
execute_process(COMMAND git -C "${freertos_kernel_SOURCE_DIR}" rev-parse HEAD
                OUTPUT_VARIABLE _frt_sha OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _frt_sha STREQUAL TLM_FREERTOS_SHA)
  message(FATAL_ERROR "FreeRTOS-Kernel ${TLM_FREERTOS_TAG} resolved to ${_frt_sha}, expected ${TLM_FREERTOS_SHA}")
endif()
set(FREERTOS_PORT_DIR ${freertos_kernel_SOURCE_DIR}/portable/GCC/ARM_CM7/r0p1)
add_library(freertos_kernel OBJECT
  ${freertos_kernel_SOURCE_DIR}/tasks.c
  ${freertos_kernel_SOURCE_DIR}/queue.c
  ${freertos_kernel_SOURCE_DIR}/list.c
  ${FREERTOS_PORT_DIR}/port.c)
# No heap_*.c: configSUPPORT_DYNAMIC_ALLOCATION is 0.
target_include_directories(freertos_kernel PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${freertos_kernel_SOURCE_DIR}/include
  ${FREERTOS_PORT_DIR})
target_link_libraries(freertos_kernel PUBLIC stm32cubemx)

# ---- Portable modules of PR #48 and PR #49, unchanged ----------------------
add_library(oamr_modules OBJECT
  ${REPO_ROOT}/firmware/telemetry/src/reset_cause.c
  ${REPO_ROOT}/firmware/telemetry/src/freshness.c
  ${REPO_ROOT}/firmware/telemetry/src/daly_rx.c
  ${REPO_ROOT}/firmware/telemetry/src/ctl017_rx.c
  ${REPO_ROOT}/firmware/sensor_bridge/src/mb7060.c
  ${REPO_ROOT}/firmware/sensor_bridge/src/tfluna.c
  ${REPO_ROOT}/firmware/canopen/src/co_cobid.c
  ${REPO_ROOT}/firmware/canopen/src/co_sdo.c
  ${REPO_ROOT}/firmware/canopen/src/co_nmt_emcy.c
  ${REPO_ROOT}/firmware/canopen/src/co_hb_consumer.c
  ${REPO_ROOT}/firmware/canopen/src/co_upload_sched.c
  ${REPO_ROOT}/firmware/drive_telemetry/src/zlac_telemetry.c)
target_include_directories(oamr_modules PUBLIC
  ${REPO_ROOT}/firmware/telemetry/include
  ${REPO_ROOT}/firmware/sensor_bridge/include
  ${REPO_ROOT}/firmware/canopen/include
  ${REPO_ROOT}/firmware/drive_telemetry/include)
set(TLM_WARNINGS -Wall -Wextra -Wconversion -Wshadow -Wstrict-prototypes)
target_compile_options(oamr_modules PRIVATE ${TLM_WARNINGS})

# ---- Build information (git commit) ----------------------------------------
set(TLM_GEN_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${TLM_GEN_DIR})
add_custom_target(tlm_build_info
  COMMAND ${CMAKE_COMMAND} -DREPO_ROOT=${REPO_ROOT} -DOUT=${TLM_GEN_DIR}/tlm_build_info.h
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_info.cmake
  BYPRODUCTS ${TLM_GEN_DIR}/tlm_build_info.h
  COMMENT "git commit for the boot banner")
add_dependencies(${CMAKE_PROJECT_NAME} tlm_build_info)

# ---- This image ------------------------------------------------------------
set(TLM_SOURCES
  src/tlm_main.c
  src/tlm_time.c
  src/tlm_console.c
  src/tlm_can1.c
  src/tlm_can1_tx.c
  src/tlm_can_aux.c
  src/tlm_sensors.c
  src/tlm_report.c)
# Pure C files (no HAL, no RTOS): built with -Wconversion and by the host tests.
set(TLM_PURE_SOURCES
  src/tlm_format.c
  src/tlm_uart_rx.c
  src/tlm_sched.c
  src/tlm_can_aux_tx.c)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
  ${TLM_SOURCES}
  ${TLM_PURE_SOURCES}
  src/cubemx_main_reuse.c
  src/cubemx_it_reuse.c)
# -Wconversion is left out for the files that include the vendor CMSIS and HAL
# headers (their inline functions trip it); the pure formatter keeps it.
set_source_files_properties(${TLM_SOURCES} PROPERTIES
  COMPILE_OPTIONS "-Wall;-Wextra;-Wshadow;-Wstrict-prototypes")
set_source_files_properties(${TLM_PURE_SOURCES} PROPERTIES COMPILE_OPTIONS "${TLM_WARNINGS}")
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${TLM_GEN_DIR})
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
  TLM_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
  $<$<BOOL:${TLM_TEST_PARAMS}>:TLM_TEST_PARAMS=1>)
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)
target_link_libraries(${CMAKE_PROJECT_NAME} freertos_kernel oamr_modules stm32cubemx)

# ---- micro-ROS client (TLM_UROS): fetched at pinned commits, not vendored ----
# uros/build_libmicroros.sh clones every repository of uros/micro_ros.pins at its
# pinned commit (a mismatch fails) and builds libmicroros.a with static pools
# (uros/colcon.meta). It runs at configure time and is skipped when the pins,
# the meta file, the toolchain file and the script are unchanged.
if(TLM_UROS)
  set(TLM_UROS_DIR ${CMAKE_BINARY_DIR}/_deps/libmicroros)
  execute_process(COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/uros/build_libmicroros.sh ${TLM_UROS_DIR}
                  RESULT_VARIABLE _uros_rc OUTPUT_FILE ${CMAKE_BINARY_DIR}/libmicroros-build.log
                  ERROR_FILE ${CMAKE_BINARY_DIR}/libmicroros-build.log)
  if(NOT _uros_rc EQUAL 0)
    message(FATAL_ERROR "libmicroros build failed (log: ${CMAKE_BINARY_DIR}/libmicroros-build.log)")
  endif()
  file(GLOB _uros_inc LIST_DIRECTORIES true ${TLM_UROS_DIR}/include/*)
  set(TLM_UROS_SOURCES src/tlm_uros.c src/tlm_uros_transport_serial.c)
  set(TLM_UROS_PURE src/tlm_uros_sm.c src/tlm_uros_alloc.c src/tlm_uros_msg.c)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${TLM_UROS_SOURCES} ${TLM_UROS_PURE})
  set_source_files_properties(${TLM_UROS_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra;-Wshadow;-Wstrict-prototypes")
  set_source_files_properties(${TLM_UROS_PURE} PROPERTIES COMPILE_OPTIONS "${TLM_WARNINGS}")
  target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE ${TLM_UROS_DIR}/include ${_uros_inc})
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TLM_UROS=1)
  target_link_libraries(${CMAKE_PROJECT_NAME} ${TLM_UROS_DIR}/libmicroros.a)
  # No heap: every C-library allocation entry goes to a fatal stop (src/tlm_uros.c).
  target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free
    -Wl,--wrap=_malloc_r,--wrap=_calloc_r,--wrap=_realloc_r,--wrap=_free_r)
endif()

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DELF=$<TARGET_FILE:${CMAKE_PROJECT_NAME}>
          -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_image.cmake
  VERBATIM)
