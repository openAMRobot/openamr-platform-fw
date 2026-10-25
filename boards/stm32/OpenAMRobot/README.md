# OpenAMRobot — STM32H723ZG CubeMX Project

## Status

**Build and linking verified in WSL; physical hardware operation not verified.**

This project is a board-support and peripheral-configuration baseline for the OpenAMRobot STM32H723 controller. It is not a complete or operational robot firmware implementation.

The project does **not** currently implement motion control, actuator control, the robot safety logic, or an operational end-to-end communication stack. Configured peripherals are not evidence that connected devices or communication paths work. In particular, CAN is configured but is not started by this baseline.

The existing Teensy 4.0 firmware remains documented separately under [`boards/teensy_4_0/linorobot2_overlay/`](../../teensy_4_0/linorobot2_overlay/README.md). This STM32 project does not claim to replace the firmware currently running on the robot.

The project has been configured and built in WSL using CMake, Ninja, and GNU Arm GCC. This verifies compilation and linking only. The firmware has not been flashed to or tested on physical hardware.

## Target and tools

- MCU: STM32H723ZGTx (`STM32H723ZGT6`)
- Package: LQFP144
- Project generator: STM32CubeMX 6.18.1
- STM32 firmware package: STM32Cube FW_H7 V1.13.0
- Build system: CMake with Ninja
- Configured system clock: 192 MHz
- Verified build environment:
  - WSL Ubuntu 24.04
  - CMake 3.28.3
  - Ninja 1.11.1
  - GNU Arm GCC 13.2.1 (`arm-none-eabi-gcc`)

Confirm the exact development-board variant and physical pin mapping against the approved hardware design before connecting peripherals.

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
| Ethernet | RMII pin configuration |

These entries describe configuration, not verified peripheral operation. They do not establish that connected devices, transceivers, physical wiring, interrupt handling, protocol drivers, or end-to-end communication have been validated.

Treat the pin assignments as a proposed configuration until checked against the approved pin map and physical board.

## Project contents

- `OpenAMRobot.ioc` — editable CubeMX configuration
- `Core/` — generated application and MCU support source
- `Drivers/` — STM32 HAL, CMSIS and Nucleo BSP files
- `cmake/` — CMake support and ARM toolchain configuration
- `CMakeLists.txt` and `CMakePresets.json` — build configuration
- `STM32H723xG_flash.ld` — linker script, including Ethernet descriptor sections in `RAM_D2`
- `startup_stm32h723xx.s` — startup assembly
- `.mxproject` — CubeMX project metadata

Retain vendor licence files, copyright notices, and other notices distributed under `Drivers/`. ST-supplied files remain subject to their applicable licence terms.
## Build prerequisites

A local build requires:

- CMake 3.22 or later
- GNU Arm Embedded toolchain (`arm-none-eabi-gcc` and companion tools)
- Ninja, used by the provided CMake presets

The verified WSL environment used CMake 3.28.3, Ninja 1.11.1, and GNU Arm GCC 13.2.1.

## Build instructions

Run these commands from this directory in an environment with the prerequisites installed.

Configure and build Debug:

```bash
cmake --preset Debug
cmake --build --preset Debug
```

Configure and build Release:

```bash
cmake --preset Release
cmake --build --preset Release
```

The presets use Ninja and the repository's `cmake/gcc-arm-none-eabi.cmake` toolchain file. Build output is placed in the corresponding `build/Debug/` or `build/Release/` directory.

## Verification evidence and limitations

The following has been verified in WSL:

- The Debug configuration builds and links an ELF executable.
- The linker script places the Ethernet descriptor sections in `RAM_D2`, outside DTCM.
- The RX descriptor table was observed at `0x30000000`.
- The TX descriptor table was observed at `0x30000060`.
- `CMakeLists.txt` declares the linker script as a link dependency.

**Not verified:** flashing, booting on the target board, physical peripheral communication, Ethernet connectivity, CAN communication, sensor readings, actuator operation, watchdog behavior, or robot-level safety behavior.

No physical hardware validation is claimed. Do not describe this project as hardware-verified until the relevant tests have actually been performed and their results recorded.

## Verification checklist

- [ ] Review `.ioc` peripheral and pin assignments against the approved pin map.
- [ ] Confirm clock-tree and peripheral timing calculations.
- [ ] Perform a clean Debug build and record the result.
- [x] Build and link the Debug configuration in WSL.
- [ ] Build and link the Release configuration.
- [ ] Review compiler warnings and generated initialization code.
- [ ] Verify peripheral communication on the intended hardware.
- [ ] Record the board revision, test setup, results, and known limitations.

## Licensing

Project-authored and CubeMX-generated board/application files are licensed under the repository's MIT licence, except where otherwise noted. Files supplied by STMicroelectronics retain their original copyright notices and applicable ST licence terms. This project does not relicense ST-provided files; retain their accompanying licence files and notices.
