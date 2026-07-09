# Contributing to OpenAMRobot Platform Firmware

Thank you for your interest in contributing to `openamr-platform-fw`.

This repository contains the low-level motor-control firmware for the Teensy 4.0 that
drives the OpenAMRobot base: micro-ROS motor control, encoder odometry, IMU integration,
and the `/debug` telemetry contract. It is an **overlay on the linorobot2 firmware**.

## Licensing of contributions

- Original contributions (documentation, new overlay logic authored here) are accepted
  under the repository **MIT** license (`LICENSE`).
- The three linorobot2-derived files (`boards/teensy_4_0/linorobot2_overlay/src/firmware.ino`,
  `.../lib/pid/pid.cpp`, `.../config/lino_base_config.h`) remain **Apache-2.0**. If you
  modify them, **keep** their Apache-2.0 header and the `Copyright (c) 2021 Juan Miguel
  Jimeno` notice, and **add a line stating what you changed** (Apache-2.0 §4(b)). Do not
  relicense these files.
- Do not add third-party code without a clearly documented, compatible license.

## Ways to contribute

- report bugs (clear, minimal reproduction; include board + toolchain versions)
- improve documentation under `docs/`
- improve the overlay (control loop, telemetry, calibration) — with a note on hardware testing
- add safety checks or clarify motion-safety behavior

## Firmware-specific guidelines

- **Test on hardware with the robot on blocks / wheels clear** before claiming a change works.
  This firmware moves motors.
- Keep the `/debug/*` topic contract stable (documented in `docs/`), or update the docs in
  the same change.
- When changing PID/feedforward/geometry constants, say what hardware you validated on.

## Commit / PR

- Sign off your commits (DCO): `git commit -s`.
- One logical change per PR; describe what you tested and on what hardware.
