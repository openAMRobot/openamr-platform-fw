# SPDX-License-Identifier: MIT
# Cross toolchain for the micro-ROS library of the TelemetryUrosSerialSimBench preset: the same
# Cortex-M7 flags as boards/stm32/OpenAMRobot/cmake/gcc-arm-none-eabi.cmake (hard float, fpv5-d16).
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_CROSSCOMPILING 1)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_C_COMPILER_WORKS 1 CACHE INTERNAL "")
set(CMAKE_CXX_COMPILER_WORKS 1 CACHE INTERNAL "")
set(_mcu "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections")
# CLOCK_MONOTONIC: newlib has no POSIX clocks; rmw_microxrcedds only needs the constant.
set(CMAKE_C_FLAGS_INIT "-std=c11 ${_mcu} -DCLOCK_MONOTONIC=0" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_INIT "-std=c++14 ${_mcu} -fno-rtti -fno-exceptions -DCLOCK_MONOTONIC=0" CACHE STRING "" FORCE)
set(__BIG_ENDIAN__ 0)
