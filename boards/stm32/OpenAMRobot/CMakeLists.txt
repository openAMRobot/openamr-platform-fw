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

set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/STM32H723xG_flash.ld"
)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Portable firmware core and adapters (firmware/), shared with the host tests
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/sources.cmake)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    ${OAMR_FIRMWARE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/Port/Src/port_app.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Port/Src/port_can.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Port/Src/port_clock.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Port/Src/port_spi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Port/Src/port_transport.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Port/Src/port_watchdog.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    ${OAMR_FIRMWARE_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/Port/Inc
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

    # Add user defined libraries
)
