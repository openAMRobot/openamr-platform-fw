cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME OpenAMRobot)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Portable, HAL-independent application building blocks. Keep board adapters
# separate from these deterministic modules.
set(OAMR_FIRMWARE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware")
add_library(oamr_common STATIC
    ${OAMR_FIRMWARE_ROOT}/common/src/time.c
    ${OAMR_FIRMWARE_ROOT}/common/src/freshness.c
    ${OAMR_FIRMWARE_ROOT}/common/src/bounded_queue.c
    ${OAMR_FIRMWARE_ROOT}/transport/src/fdcan_recovery.c
)
target_include_directories(oamr_common PUBLIC
    ${OAMR_FIRMWARE_ROOT}/common/include
    ${OAMR_FIRMWARE_ROOT}/transport/include
)
add_library(oamr_sensor_bridge STATIC
    ${OAMR_FIRMWARE_ROOT}/sensor_bridge/src/mb7060.c
    ${OAMR_FIRMWARE_ROOT}/sensor_bridge/src/tfluna.c
    ${OAMR_FIRMWARE_ROOT}/sensor_bridge/src/sensor_scheduler.c
    ${OAMR_FIRMWARE_ROOT}/sensor_bridge/src/dma_cursor.c
)
target_include_directories(oamr_sensor_bridge PUBLIC
    ${OAMR_FIRMWARE_ROOT}/common/include
    ${OAMR_FIRMWARE_ROOT}/sensor_bridge/include
)

set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/STM32H723xG_flash.ld"
)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    App/Src/sensor_dma.c
    App/Src/mb7060_sensor.c
    App/Src/sensor_uart_callbacks.c
    App/Src/fdcan_adapter.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    App/Inc
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    oamr_common
    oamr_sensor_bridge

    # Add user defined libraries
)
