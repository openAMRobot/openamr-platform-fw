/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
/*
 * Compile-time guard (SAF-MCU-001): every reference source includes this header through
 * simref_can.h, simref_sm.h or simref_permit.h, so a reference source added to a bare-metal
 * ARM target (STM32H723, Teensy 4.x: arm-none-eabi, no __linux__) stops at the preprocessor.
 * Hosted Linux on ARM (Jetson, aarch64 or armhf) is not refused here. There is no override macro.
 */
#ifndef SIMREF_HOST_ONLY_H
#define SIMREF_HOST_ONLY_H

#if (defined(__arm__) || defined(__thumb__) || defined(__ARM_ARCH_7EM__)) && !defined(__linux__)
#error "SIMULATION ONLY: tests/sim_reference must not be compiled for a board target (bare-metal ARM)"
#endif

#endif
