# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.16)

project(oamr_host_sensor_tests LANGUAGES C)

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

add_library(oamr_sensor_bridge STATIC
    ../../../firmware/sensor_bridge/src/mb7060.c
    ../../../firmware/sensor_bridge/src/tfluna.c
    ../../../firmware/sensor_bridge/src/sensor_scheduler.c
    ../../../firmware/sensor_bridge/src/dma_cursor.c
)
target_include_directories(oamr_sensor_bridge PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/common/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/sensor_bridge/include"
)
oamr_enable_strict_warnings(oamr_sensor_bridge)

function(oamr_add_host_test target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE oamr_sensor_bridge)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../common")
    oamr_enable_strict_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
endfunction()

oamr_add_host_test(test_mb7060 test_mb7060.c)
oamr_add_host_test(test_tfluna test_tfluna.c)
oamr_add_host_test(test_sensor_scheduler test_sensor_scheduler.c)
oamr_add_host_test(test_mb7060_adapter
    test_mb7060_adapter.c
    ../../../boards/stm32/OpenAMRobot/App/Src/mb7060_sensor.c
)
target_compile_definitions(test_mb7060_adapter PRIVATE MB7060_SENSOR_HOST_TEST=1)
target_include_directories(test_mb7060_adapter PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../boards/stm32/OpenAMRobot/App/Inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs/mb7060"
)
oamr_add_host_test(test_dma_cursor test_dma_cursor.c)
oamr_add_host_test(test_sensor_dma_adapter_configured
    test_sensor_dma_adapter.c
    ../../../boards/stm32/OpenAMRobot/App/Src/sensor_dma.c
)
target_compile_definitions(test_sensor_dma_adapter_configured PRIVATE SENSOR_DMA_TEST_CONFIGURED=1)
target_include_directories(test_sensor_dma_adapter_configured PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../boards/stm32/OpenAMRobot/App/Inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
)
oamr_add_host_test(test_sensor_dma_adapter
    test_sensor_dma_adapter.c
    ../../../boards/stm32/OpenAMRobot/App/Src/sensor_dma.c
)
target_include_directories(test_sensor_dma_adapter PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../boards/stm32/OpenAMRobot/App/Inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
)
