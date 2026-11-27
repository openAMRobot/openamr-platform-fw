# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.16)

project(oamr_host_common_interface_tests LANGUAGES C)

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

function(oamr_add_host_test target)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../common"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/common/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/transport/include"
    )

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

    add_test(NAME ${target} COMMAND ${target})
endfunction()

oamr_add_host_test(test_common_interfaces
    test_common_interfaces.c
    ../../../firmware/common/src/time.c
    ../../../firmware/common/src/freshness.c
    ../../../firmware/common/src/bounded_queue.c
)
