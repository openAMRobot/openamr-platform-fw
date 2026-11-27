# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.16)

project(oamr_host_diagnostics_tests LANGUAGES C)

option(OAMR_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    if(OAMR_SANITIZE)
        if(WIN32)
            message(FATAL_ERROR
                "OAMR_SANITIZE=ON is not supported by this project on Windows. "
                "Configure with -DOAMR_SANITIZE=OFF.")
        endif()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

enable_testing()

function(oamr_enable_strict_warnings target)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Werror
            -Wconversion
            -Wshadow
            -Wstrict-prototypes
            -Wpedantic
        )
    endif()
endfunction()

# PR6 production sources and their reviewed public contracts are prerequisites.
# Do not invent replacement APIs merely to make this host project configurable.
set(OAMR_DIAGNOSTICS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
set(OAMR_DIAGNOSTICS_INCLUDE_DIR
    "${OAMR_DIAGNOSTICS_ROOT}/firmware/diagnostics/include")
set(OAMR_DIAGNOSTICS_SOURCE_DIR
    "${OAMR_DIAGNOSTICS_ROOT}/firmware/diagnostics/src")
set(OAMR_DIAGNOSTICS_UART_SOURCE
    "${OAMR_DIAGNOSTICS_ROOT}/boards/stm32/OpenAMRobot/App/Src/diagnostics_uart.c")

foreach(required_path IN ITEMS
        "${OAMR_DIAGNOSTICS_INCLUDE_DIR}"
        "${OAMR_DIAGNOSTICS_SOURCE_DIR}"
        "${OAMR_DIAGNOSTICS_UART_SOURCE}")
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR
            "Diagnostics host tests require the PR6 production path: "
            "${required_path}. Test sources must be written against the actual "
            "public APIs; no substitute production implementation is provided.")
    endif()
endforeach()

set(OAMR_DIAGNOSTICS_COMMON_INCLUDE_DIR
    "${OAMR_DIAGNOSTICS_ROOT}/firmware/common/include")
set(OAMR_DIAGNOSTICS_TRANSPORT_INCLUDE_DIR
    "${OAMR_DIAGNOSTICS_ROOT}/firmware/transport/include")
set(OAMR_DIAGNOSTICS_BOARD_INCLUDE_DIR
    "${OAMR_DIAGNOSTICS_ROOT}/boards/stm32/OpenAMRobot/App/Inc")

add_library(oamr_diagnostics_portable STATIC
    "${OAMR_DIAGNOSTICS_SOURCE_DIR}/reset_cause.c"
    "${OAMR_DIAGNOSTICS_SOURCE_DIR}/fault_record.c"
    "${OAMR_DIAGNOSTICS_SOURCE_DIR}/diagnostic_tx.c"
)
target_include_directories(oamr_diagnostics_portable PUBLIC
    "${OAMR_DIAGNOSTICS_INCLUDE_DIR}"
    "${OAMR_DIAGNOSTICS_COMMON_INCLUDE_DIR}"
    "${OAMR_DIAGNOSTICS_TRANSPORT_INCLUDE_DIR}"
)
oamr_enable_strict_warnings(oamr_diagnostics_portable)

function(oamr_add_diagnostics_test target)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../common"
        "${OAMR_DIAGNOSTICS_INCLUDE_DIR}"
        "${OAMR_DIAGNOSTICS_COMMON_INCLUDE_DIR}"
        "${OAMR_DIAGNOSTICS_TRANSPORT_INCLUDE_DIR}"
    )
    oamr_enable_strict_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
endfunction()

oamr_add_diagnostics_test(test_reset_cause test_reset_cause.c
    "${OAMR_DIAGNOSTICS_SOURCE_DIR}/reset_cause.c")
oamr_add_diagnostics_test(test_fault_record test_fault_record.c
    "${OAMR_DIAGNOSTICS_SOURCE_DIR}/fault_record.c")
oamr_add_diagnostics_test(test_diagnostic_tx test_diagnostic_tx.c)
target_link_libraries(test_diagnostic_tx PRIVATE oamr_diagnostics_portable)

add_library(oamr_diagnostics_uart_adapter STATIC
    "${OAMR_DIAGNOSTICS_UART_SOURCE}")
target_include_directories(oamr_diagnostics_uart_adapter PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
    "${OAMR_DIAGNOSTICS_BOARD_INCLUDE_DIR}"
    "${OAMR_DIAGNOSTICS_INCLUDE_DIR}"
    "${OAMR_DIAGNOSTICS_COMMON_INCLUDE_DIR}"
    "${OAMR_DIAGNOSTICS_TRANSPORT_INCLUDE_DIR}"
)
oamr_enable_strict_warnings(oamr_diagnostics_uart_adapter)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # glibc exposes strnlen in strict C11 only with a POSIX feature level.
    # The unsigned conditional expression is bounded by the 1024-byte ring,
    # but GCC cannot prove that its result always fits diagnostics' uint16_t.
    target_compile_definitions(oamr_diagnostics_uart_adapter PRIVATE
        _POSIX_C_SOURCE=200809L)
endif()

add_executable(test_diagnostics_uart test_diagnostics_uart.c)
target_include_directories(test_diagnostics_uart PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../common"
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
    "${OAMR_DIAGNOSTICS_BOARD_INCLUDE_DIR}"
)
target_link_libraries(test_diagnostics_uart PRIVATE
    oamr_diagnostics_uart_adapter
    oamr_diagnostics_portable
)
oamr_enable_strict_warnings(test_diagnostics_uart)
add_test(NAME test_diagnostics_uart COMMAND test_diagnostics_uart)

