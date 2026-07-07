# Authors and Contributors

This repository is maintained by the OpenAMRobot organization.

## Maintainer

- OpenAMRobot organization
- Contact: botshare.ai@gmail.com

## Contributors

Recognition is given to contributors whose work has materially shaped this repository.
Listing here does not replace GitHub history — it complements it by making non-trivial
contributions easy to find for new readers, students, and downstream users.

### Firmware overlay, control loop, and documentation

- **Matthieu Vinet** — [@SHuttooo](https://github.com/SHuttooo)
  - **Teensy 4.0 `linorobot2_overlay`** brought to the firmware deployed on the real robot:
    - **Feedforward + dither** on top of the PID, and **back-calculation anti-windup** on the
      integral term.
    - **Runtime encoder-ripple correction table** (`/debug/enc_cal`) — hot-loaded and
      phase-aligned per boot to flatten the AS5040 eccentricity ripple (~±40 % → ~±4 %).
    - **`/debug` telemetry + tuning contract**: `/debug/left`, `/debug/right`, `/debug/pwm`
      (per-wheel telemetry), and the `/debug/openloop`, `/debug/tune`, `/debug/enc_cal`
      command topics; plus a bounded open-loop test mode (`ENABLE_POWERED_DEBUG`).
    - **MPU6500 IMU workaround** (driven through the MPU9250 driver via the WHO_AM_I check).
    - Real-robot **PID / motor tuning** (`K_P 2.0 / K_I 0.1 / K_D 0.1`, `MOTOR2_GAIN`) and the
      measured drivetrain constants in `config/lino_base_config.h`.
  - **Firmware documentation set**: control loop, `/debug` telemetry, encoder ripple
    calibration, micro-ROS bring-up, build & flash, motion safety, and troubleshooting.
  - **Licensing**: MIT for the original work + the Apache-2.0 carve-out and `NOTICE` for the
    linorobot2-derived overlay files.

### Repository scaffolding

- **Alex** ([OpenAMRobot maintainer](mailto:botshare.ai@gmail.com))
  - Initial `openamr-platform-fw` repository structure and governance scaffolding.

## Upstream / third-party

This firmware is an overlay on **linorobot2** (Copyright (c) 2021 Juan Miguel Jimeno,
Apache-2.0). The linorobot2-derived files remain Apache-2.0 — see `NOTICE.md`.
