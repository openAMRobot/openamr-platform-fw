# SPDX-License-Identifier: MIT
# Toolchain file of the telemetry image: the generated toolchain file of
# boards/stm32/OpenAMRobot, unchanged, with the linker-script path pointed at
# that project's STM32H723xG_flash.ld (the generated file derives it from
# CMAKE_SOURCE_DIR, which is this directory here).
get_filename_component(OPENAMR_CUBEMX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../OpenAMRobot" ABSOLUTE)
include("${OPENAMR_CUBEMX_DIR}/cmake/gcc-arm-none-eabi.cmake")
string(REGEX REPLACE "-T \"[^\"]*\"" "-T \"${OPENAMR_CUBEMX_DIR}/STM32H723xG_flash.ld\""
       CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
