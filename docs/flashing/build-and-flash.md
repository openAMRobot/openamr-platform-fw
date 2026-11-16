# Build & flash (Teensy 4.0)

*Applies to the Gate A (legacy) Teensy 4.0 `linorobot2_overlay` firmware, also the release fallback;
not the 2.0 STM32H723ZG firmware.*

The overlay is built on top of a linorobot2 firmware checkout with PlatformIO and flashed to the
Teensy 4.0 with `teensy_loader_cli`. See the [overlay README](../../boards/teensy_4_0/linorobot2_overlay/README.md)
for how the overlay files map onto the linorobot2 base.

## Pinned build (Gate A, deployed 2026-06-29)

The Teensy firmware on the robot was built on 2026-06-29 from the inputs below, confirmed on the
build machine ([#13](https://github.com/openAMRobot/openamr-platform-fw/issues/13)). Use the same
inputs for any Gate A build so it can be reproduced.

| Input | Pinned value |
|---|---|
| linorobot2_hardware base | `aaf9d59cd18c0cd1905be6fdae9ea5c99961a766` (branch `jazzy`) |
| PlatformIO platform | `teensy@5.1.0` |
| framework-arduinoteensy | 1.160.0 |
| toolchain-gccarmnoneeabi-teensy | 1.110301.0 (GCC 11.3.1) |
| micro-ROS distro | `jazzy` (`ROS_DISTRO=jazzy`) |
| Libraries | pinned in the overlay's `firmware/platformio.ini` |

Deployed `firmware.hex`: sha256 `f7e52cbca3be191bf6c0ec599550b086433d5ec690745f2bf8351d5154253ef4`.

### Prepare the checkout

```bash
git clone -b jazzy https://github.com/linorobot/linorobot2_hardware.git
cd linorobot2_hardware
git checkout aaf9d59cd18c0cd1905be6fdae9ea5c99961a766
OVL=<openamr-platform-fw>/boards/teensy_4_0/linorobot2_overlay
cp $OVL/config/lino_base_config.h config/
cp $OVL/firmware/platformio.ini   firmware/
cp $OVL/src/firmware.ino          firmware/src/
cp $OVL/lib/pid/pid.cpp $OVL/lib/pid/pid.h firmware/lib/pid/
cp $OVL/lib/encoder/encoder.h     firmware/lib/encoder/
git status --short   # expect exactly these 6 files modified
```

### Reproducibility (checked 2026-11-02)

A clean rebuild with these pins on an x86_64 host builds successfully, and the application
symbols are identical to the deployed `firmware.elf`. It is **not byte-identical**: text is
10,240 B smaller and bss 704 B smaller, all in newlib (C library) and micro-ROS symbols.
Two known causes:

- `micro_ros_platformio` clones the ROS 2 packages (rcl, rclc, rmw, …) from the `jazzy`
  branch at build time, so its sources are not pinned by the library commit.
- The toolchain package may differ by host architecture. The deployed build reports
  `GCC: (GNU) 11.3.1`; the x86_64 rebuild reports `GCC: (Arm GNU Toolchain 11.3.Rel1) 11.3.1`.
  The build machine's architecture is not yet confirmed.

Not pinned: PlatformIO Core (deployed build used 6.1.19) and the upload tool `tool-teensy`.

## Build

PlatformIO builds the `teensy40` environment, which uses `config/lino_base_config.h` (**not** any
`dev_config.h`).

```bash
cd <linorobot2_hardware>/firmware
ROS_DISTRO=jazzy ~/.platformio/penv/bin/pio run -e teensy40
```

- On Ubuntu 24.04, PlatformIO **must** be run from its own venv (`~/.platformio/penv/bin/pio`, as
  above) to avoid the PEP 668 "externally managed environment" restriction — a bare `pio` may fail.
- First build ≈ 20 min (compiles micro-ROS); incremental ≈ 8 s.
- The build output is `.pio/build/teensy40/firmware.hex`.

## Flash

Install `teensy_loader_cli` and the PJRC udev rules (`/etc/udev/rules.d/00-teensy.rules`). **Stop
the micro-ROS agent first** to free the serial port.

```bash
pkill -f "[m]icro_ros_agent"
HEX=<linorobot2_hardware>/firmware/.pio/build/teensy40/firmware.hex
sudo teensy_loader_cli --mcu=TEENSY40 -s -w -v "$HEX"   # -s = soft reboot into the bootloader
```

> ⚠️ **`-s` (soft reboot) is timing-flaky** and may report `error writing`. When that happens the
> board is already in **HalfKay** (bootloader; LED off, and it stays there). Retry once **without**
> `-s`:
> ```bash
> sudo teensy_loader_cli --mcu=TEENSY40 -w -v "$HEX"
> ```
> The most reliable method is to press the Teensy's **physical button** to force HalfKay, then flash
> with `-w`. A USB unplug/replug after a failed flash boots the last good firmware.

After flashing, restart the micro-ROS agent (or the host bring-up). See
[micro-ROS bringup](../bringup/micro-ros-bringup.md).

> ⚠️ **A reflash reboots the Teensy and shifts the encoder zero.** If you use the encoder ripple
> table, re-run the host alignment after every flash/power-cycle — see
> [encoder calibration](../architecture/encoder-calibration.md).
