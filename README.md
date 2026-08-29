# OpenAMR Platform Firmware

Low-level motor-control firmware for the **OpenAMRobot** mobile base: micro-ROS motor
control, encoder odometry, IMU integration, and a `/debug` telemetry/tuning contract, running
on a **Teensy 4.0**.

**Status: experimental.** The firmware currently ships as a **[Teensy 4.0 linorobot2
overlay](boards/teensy_4_0/linorobot2_overlay/README.md)** — an overlay on top of the
[linorobot2](https://github.com/linorobot/linorobot2) firmware (Apache-2.0). The modular
`firmware/` and `configs/` directories are placeholders for a planned decomposition; the
overlay is what runs on the robot today.

> [!NOTE]
> This repository is part of the **OpenAMRobot vX.X.X** release.
>
> Download the complete product release (Hardware + Software + Firmware + UI + Documentation) here:
>
> **https://github.com/openAMRobot/openamrobot-release/releases/latest**

📖 **[README](README.md)** ·
🏗️ **[Control loop](docs/architecture/control-loop.md)** ·
🔧 **[Debug/tuning interface](docs/architecture/debug-telemetry.md)** ·
📐 **[Encoder calibration](docs/architecture/encoder-calibration.md)** ·
🔌 **[Build & flash](docs/flashing/build-and-flash.md)** ·
🛡️ **[Motion safety](docs/safety/motion-safety.md)** ·
🧰 **[Troubleshooting](docs/troubleshooting/common-issues.md)** ·
📜 **[License](LICENSE)** · 📝 **[Changelog](CHANGELOG.md)** · ℹ️ **[Notice](NOTICE.md)**

> ⚠️ **This firmware drives real motors.** Some debug paths move the motors on command — in
> particular the raw-PWM **`/debug/openloop`** path, gated by the `ENABLE_POWERED_DEBUG` build
> flag. Keep the robot on blocks / clear of people when any powered debug feature is enabled.
> See [motion safety](docs/safety/motion-safety.md).

---

## Prerequisites

- **ROS 2 Jazzy** (host side, for the micro-ROS agent and to see the topics)
- **PlatformIO** (builds the Teensy firmware; pulls the micro-ROS + Teensy toolchain)
- **`teensy_loader_cli`** (or the Teensy Loader GUI) to flash the `.hex`
- **PJRC Teensy udev rules** on Linux (so the board is flashable without root):
  `00-teensy.rules` from https://www.pjrc.com/teensy/00-teensy.rules → `/etc/udev/rules.d/`
- **micro-ROS agent** (run on the host to bridge the Teensy to ROS 2):
  `ros2 run micro_ros_agent micro_ros_agent serial --dev /dev/ttyACM0`
- CycloneDDS as RMW is recommended for the rest of the stack
  (`export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp`).

## Quick start

```bash
# 1. Obtain the linorobot2 firmware base (see the pin note below) and apply this overlay.
#    The overlay README lists exactly which files to copy over a linorobot2 checkout:
#    boards/teensy_4_0/linorobot2_overlay/{src/firmware.ino, lib/pid/pid.cpp,
#    config/lino_base_config.h}
#    -> see boards/teensy_4_0/linorobot2_overlay/README.md

# 2. Build + flash the Teensy 4.0 (details in docs/flashing/build-and-flash.md)
pio run                     # build
pio run -t upload           # or: teensy_loader_cli --mcu=TEENSY40 -w -v .pio/build/*/firmware.hex

# 3. Start the micro-ROS agent on the host (robot IMMOBILE — gyro bias is captured at boot)
ros2 run micro_ros_agent micro_ros_agent serial --dev /dev/ttyACM0
```

### Obtaining the linorobot2 base (and pinning it)

The overlay is applied **on top of a linorobot2 firmware checkout**. For a reproducible build
you must pin the exact upstream version:

```bash
# The microcontroller firmware lives in linorobot2_hardware (not the linorobot2 ROS 2 repo).
git clone https://github.com/linorobot/linorobot2_hardware.git
cd linorobot2_hardware
git checkout aaf9d59cd18c0cd1905be6fdae9ea5c99961a766   # branch jazzy, 2026-04-30 (see NOTICE.md)
# then copy the three overlay files from
# boards/teensy_4_0/linorobot2_overlay/ over the matching linorobot2_hardware files.
```

> **Pinned upstream:** `linorobot2_hardware`, branch `jazzy`, commit
> `aaf9d59cd18c0cd1905be6fdae9ea5c99961a766` (2026-04-30). The full provenance — and how it was
> verified against the upstream history — is in [`NOTICE.md`](NOTICE.md); see also the overlay
> [README](boards/teensy_4_0/linorobot2_overlay/README.md).

### Post-flash verification checklist

After flashing, with the robot **on blocks / wheels clear of the ground**:

1. **Agent connects** — `micro_ros_agent` prints a session and does not loop-reset.
2. **Topics appear** — `ros2 topic list` shows `/odom/unfiltered` (or `/odom`), `/imu/data_raw`,
   `/imu/mag`, and the `/debug/*` topics.
3. **IMU sane at rest** — `ros2 topic echo /imu/data_raw` shows gyro ≈ 0 when the robot is still
   (if it drifts, power-cycle the Teensy **immobile** to re-capture the gyro bias at boot).
4. **Encoder directions** — nudge each wheel forward by hand; confirm the encoder counts move
   the expected sign (`/debug/left`, `/debug/right`). Fix `MOTORx_ENCODER_INV` if reversed.
5. **Low-speed `/cmd_vel`** — publish a small `/cmd_vel` and confirm both wheels turn the right
   way at similar rates. Re-run the encoder calibration afterwards (below).

### Encoder ripple calibration — host-side tools (vendored)

The encoder ripple correction table is **re-aligned per boot** by host-side scripts, vendored in
this repo under [`tools/encoder-calibration/`](tools/encoder-calibration/):

- `align_enc_cal.py` (run wheels-in-the-air, ~8 s) + `encoder_ref_table.json` — the per-boot
  alignment ritual, run after **every Teensy power-cycle** (an incremental encoder loses phase at
  boot).

See [encoder calibration](docs/architecture/encoder-calibration.md) for the engineering story and
[`tools/encoder-calibration/README.md`](tools/encoder-calibration/README.md) for the full workflow.

---

## Repository structure

```text
openamr-platform-fw/
├── boards/
│   └── teensy_4_0/linorobot2_overlay/   ← the current firmware (overlay on linorobot2)
│       ├── src/firmware.ino             (Apache-2.0, modified from linorobot2)
│       ├── lib/pid/pid.cpp              (Apache-2.0, modified from linorobot2)
│       └── config/lino_base_config.h    (Apache-2.0, modified from linorobot2)
├── docs/                                ← firmware documentation
├── tools/
│   └── encoder-calibration/             ← host-side ripple-calibration scripts (per-boot align)
├── firmware/                            ← PLACEHOLDER (planned modular decomposition)
├── configs/                             ← PLACEHOLDER (planned per-module configs)
└── tests/                               ← PLACEHOLDER
```

## Documentation

- Architecture — [control loop & motor control](docs/architecture/control-loop.md),
  [debug & tuning interface](docs/architecture/debug-telemetry.md),
  [encoder ripple calibration](docs/architecture/encoder-calibration.md)
- [micro-ROS bringup](docs/bringup/micro-ros-bringup.md)
- [build & flash (Teensy 4.0)](docs/flashing/build-and-flash.md)
- [motion safety](docs/safety/motion-safety.md)
- [troubleshooting](docs/troubleshooting/common-issues.md)
- [overlay module mapping](boards/teensy_4_0/linorobot2_overlay/README.md)

## Repository boundaries

Firmware belongs here. Related repositories:

- **`openamr-platform-sw`** — ROS 2 software (description, Gazebo, Nav2, docking).
- **`openamr-platform-hw`** — hardware: CAD, BOM, wiring, power, safety.

## License

**MIT** for the original work authored by OpenAMRobot (documentation and the new overlay
logic) — see [`LICENSE`](LICENSE).

The three overlay files derived from **linorobot2** (`src/firmware.ino`, `lib/pid/pid.cpp`,
`config/lino_base_config.h`) remain under the **Apache License 2.0** (Copyright (c) 2021 Juan
Miguel Jimeno) — see [`LICENSE-Apache-2.0`](LICENSE-Apache-2.0) and [`NOTICE.md`](NOTICE.md).

## Ownership, licensing, and contributions

OpenAMRobot is a project initiated, operated, and controlled by **Botshare LTD** (Cyprus Company ID HE479056). Botshare LTD owns the transferable economic rights in original OpenAMRobot material created by or validly assigned to it. Third-party material remains subject to its respective ownership, licences, and notices.

Original OpenAMRobot software and firmware are licensed under MIT, documentation under CC BY 4.0, and hardware design source under CERN-OHL-P-2.0, as mapped in [`LICENSING.md`](LICENSING.md). Public distribution grants the permissions stated in the applicable licence; it does not transfer ownership of underlying copyright, trademarks, patents, or other intellectual property.

Accepted external contributions require DCO sign-off and an applicable Individual or Corporate Contributor Agreement governing assignment of transferable economic rights to Botshare LTD. Contributor attribution and legally non-waivable authorship or moral rights remain recognized.

See the organization [IP Policy](https://github.com/openAMRobot/.github/blob/main/IP_POLICY.md), [Contribution Guide](https://github.com/openAMRobot/.github/blob/main/CONTRIBUTING.md), and [Contributor Agreement Process](https://github.com/openAMRobot/.github/blob/main/CLA.md).
