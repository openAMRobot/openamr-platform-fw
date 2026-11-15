# SPDX-License-Identifier: MIT
# Host test build of the pure parts of the telemetry image and of its FDCAN
# transmit function (boards/stm32/telemetry_image/src), compiled unchanged.
#
#   cmake -S tests/host/telemetry_image -B build/host-telemetry-image
#   cmake --build build/host-telemetry-image
#   ctest --test-dir build/host-telemetry-image --output-on-failure --no-tests=error
cmake_minimum_required(VERSION 3.22)
project(openamr_telemetry_image_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(IMG ${REPO_ROOT}/boards/stm32/telemetry_image)
set(WARN -Wall -Wextra -Werror -Wconversion -Wshadow -Wstrict-prototypes -Wpedantic)
option(OAMR_SANITIZE "Build with address and undefined-behaviour sanitizers" ON)
if(OAMR_SANITIZE)
  add_compile_options(-g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
enable_testing()

set(INC ${IMG}/include ${IMG}/src ${REPO_ROOT}/firmware/canopen/include)

# The transmit function with the recording HAL stub (stub/main.h). The image
# headers come after the stub directory, so its main.h is the one found.
add_executable(test_can1_tx test_can1_tx.c ${IMG}/src/tlm_can1_tx.c)
target_include_directories(test_can1_tx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stub
                           ${CMAKE_CURRENT_SOURCE_DIR} ${INC})
target_compile_options(test_can1_tx PRIVATE ${WARN})
add_test(NAME test_can1_tx COMMAND test_can1_tx)

# The CAN2/CAN3 gate, linked with the CAN1 gate and the same recording stub.
add_executable(test_can_aux_tx test_can_aux_tx.c ${IMG}/src/tlm_can_aux_tx.c ${IMG}/src/tlm_can1_tx.c)
target_include_directories(test_can_aux_tx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stub
                           ${CMAKE_CURRENT_SOURCE_DIR} ${INC})
target_compile_options(test_can_aux_tx PRIVATE ${WARN})
add_test(NAME test_can_aux_tx COMMAND test_can_aux_tx)

add_executable(test_uart_rx test_uart_rx.c ${IMG}/src/tlm_uart_rx.c ${IMG}/src/tlm_sched.c)
target_include_directories(test_uart_rx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${INC})
target_compile_options(test_uart_rx PRIVATE ${WARN})
add_test(NAME test_uart_rx COMMAND test_uart_rx)

add_executable(test_format test_format.c ${IMG}/src/tlm_format.c)
target_include_directories(test_format PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${INC})
target_compile_options(test_format PRIVATE ${WARN})
add_test(NAME test_format COMMAND test_format)
