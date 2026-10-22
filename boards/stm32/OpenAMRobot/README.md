# OpenAMRobot — STM32H723ZG CubeMX Project

## Status

**Build verified in WSL; physical hardware operation not yet verified.**

The project has been configured and built successfully in WSL using CMake,
Ninja, and GNU Arm GCC. The resulting ELF was linked successfully.

This verifies compilation and linking only. The firmware has not been flashed
to or tested on physical hardware. Review compiler output and verify the clock
configuration, peripheral assignments, and hardware operation before treating
the port as functional.

This directory contains the STM32CubeMX project and generated C/CMake startup
and peripheral-initialization files for the OpenAMRobot STM32 controller port.
It is a configuration baseline, not evidence of a complete or operational
motor-control firmware implementation.

The existing Teensy 4.0 firmware remains documented separately under
[`boards/teensy_4_0/linorobot2_overlay/`](../../teensy_4_0/linorobot2_overlay/README.md).

## Target

- MCU: STM32H723ZGTx (`STM32H723ZGT6`)
- Package: LQFP144
- Project generator: STM32CubeMX
- Build system: CMake
- Configured system clock: 192 MHz

Confirm the exact development-board variant and physical pin mapping against
the hardware design before connecting peripherals.

## Configured peripheral baseline

The current `OpenAMRobot.ioc` records these settings:

| Peripheral | Configuration |
|---|---|
| FDCAN1 | Nominal bitrate 500 kbit/s |
| FDCAN2 | Nominal bitrate 250 kbit/s |
| FDCAN3 | Nominal bitrate 500 kbit/s |
| SPI4 | Master, 8-bit, two-line, configured baud rate 6 Mbit/s |
| UART4 | Receive-only, 9600 baud, RX inversion enabled |
| UART5 | Receive-only, 9600 baud, RX inversion enabled |
| UART7 | Receive-only, 9600 baud, RX inversion enabled |
| UART8 | Receive-only, 9600 baud, RX inversion enabled |
| USART2 | Asynchronous mode selected |
| USART6 | Asynchronous mode selected |

These are CubeMX configuration settings. They do not establish that connected
devices, transceivers, physical wiring, interrupt handling, protocol drivers,
or end-to-end communication have been validated.

## Project contents

- `OpenAMRobot.ioc` — editable CubeMX configuration
- `Core/` — generated application and MCU support source
- `Drivers/` — STM32 HAL, CMSIS and Nucleo BSP files
- `cmake/` — generated CMake support
- `CMakeLists.txt` and `CMakePresets.json` — build configuration
- `STM32H723xG_flash.ld` — linker script
- `startup_stm32h723xx.s` — startup assembly
- `.mxproject` — CubeMX project metadata

Retain the vendor licence and notice files distributed under `Drivers/`.

## Build prerequisites

A local build requires:

- CMake 3.22 or later
- GNU Arm Embedded toolchain (`arm-none-eabi-gcc` and companion tools)
- The build tool and generator required by the selected CMake preset

Build and linking have been verified in WSL using CMake, Ninja, and GNU Arm GCC.
This does not verify physical hardware operation. Review compiler warnings,
clock configuration, peripheral assignments, and hardware behavior before
treating the port as functional.

## Verification checklist

- [ ] Review `.ioc` peripheral and pin assignments against the approved pin map.
- [ ] Confirm clock-tree and peripheral timing calculations.
- [ ] Configure the required compiler and CMake generator.
- [ ] Build successfully from a clean build directory.
- [ ] Review compiler warnings and generated initialization code.
- [ ] Verify peripheral communication on the intended hardware.
- [ ] Record the board revision, test setup, results and any known limitations.

Do not describe the port as hardware-verified until the relevant tests have
actually been performed.

## Licensing

Project-authored and CubeMX-generated board/application files are licensed
under the repository's MIT license, except where otherwise noted. Files
supplied by STMicroelectronics retain their original copyright notices and
applicable ST license terms. This project does not relicense ST-provided files;
retain their accompanying license files and notices.
