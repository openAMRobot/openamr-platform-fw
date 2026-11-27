# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.16)

project(oamr_host_can_tests LANGUAGES C)

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

set(OAMR_CAN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
set(OAMR_CAN_RECOVERY_SOURCE
    "${OAMR_CAN_ROOT}/firmware/transport/src/fdcan_recovery.c")
set(OAMR_CAN_ADAPTER_SOURCE
    "${OAMR_CAN_ROOT}/boards/stm32/OpenAMRobot/App/Src/fdcan_adapter.c")

foreach(required_source IN ITEMS
        "${OAMR_CAN_RECOVERY_SOURCE}"
        "${OAMR_CAN_ADAPTER_SOURCE}")
    if(NOT EXISTS "${required_source}")
        message(FATAL_ERROR
            "CAN host tests require the PR5 production source: ${required_source}. "
            "No substitute API or simulated production implementation is provided.")
    endif()
endforeach()

add_library(oamr_fdcan_recovery STATIC
    "${OAMR_CAN_RECOVERY_SOURCE}"
    "${OAMR_CAN_ROOT}/firmware/common/src/time.c"
    "${OAMR_CAN_ROOT}/firmware/common/src/bounded_queue.c"
)
target_include_directories(oamr_fdcan_recovery PUBLIC
    "${OAMR_CAN_ROOT}/firmware/common/include"
    "${OAMR_CAN_ROOT}/firmware/transport/include"
)
oamr_enable_strict_warnings(oamr_fdcan_recovery)

function(oamr_add_can_test target)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../common"
        "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
        "${OAMR_CAN_ROOT}/firmware/common/include"
        "${OAMR_CAN_ROOT}/firmware/transport/include"
        "${OAMR_CAN_ROOT}/boards/stm32/OpenAMRobot/App/Inc"
    )
    oamr_enable_strict_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
endfunction()

oamr_add_can_test(test_fdcan_recovery test_fdcan_recovery.c)
target_link_libraries(test_fdcan_recovery PRIVATE oamr_fdcan_recovery)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_fdcan_adapter.c")
    add_library(oamr_fdcan_adapter STATIC "${OAMR_CAN_ADAPTER_SOURCE}")
    target_include_directories(oamr_fdcan_adapter PUBLIC
        "${OAMR_CAN_ROOT}/firmware/common/include"
        "${OAMR_CAN_ROOT}/firmware/transport/include"
        "${OAMR_CAN_ROOT}/boards/stm32/OpenAMRobot/App/Inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
    )
    oamr_enable_strict_warnings(oamr_fdcan_adapter)
    # The delivered adapter currently narrows HAL 32-bit counters and a
    # timestamp expression. Keep every other warning fatal without modifying
    # production sources in this test-only change.
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    endif()

    oamr_add_can_test(test_fdcan_adapter test_fdcan_adapter.c)
    target_link_libraries(test_fdcan_adapter PRIVATE
        oamr_fdcan_adapter
        oamr_fdcan_recovery
    )
endif()
