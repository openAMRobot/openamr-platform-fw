# Notice

OpenAMRobot and OpenAMR are project names maintained by the OpenAMRobot organization.

This repository contains the low-level motor-control firmware, documentation, and
configuration for the Teensy 4.0 that drives the OpenAMRobot mobile base.

## Project Ownership

The OpenAMRobot organization maintains this repository and its project direction.
Contributors retain visibility and attribution through GitHub commits, Pull Requests,
and release notes where applicable. Contributions are accepted under the terms in
`CONTRIBUTING.md` and the repository `LICENSE`.

## Licensing summary

- Original work authored by OpenAMRobot (documentation and the new overlay logic) is
  licensed **MIT** — see `LICENSE`.
- Files derived from **linorobot2** remain licensed **Apache-2.0** — see below and
  `LICENSE-Apache-2.0`.

## Third-Party Material — linorobot2 (Apache-2.0)

This firmware is built as an **overlay on top of the linorobot2 firmware**.

- **Upstream project:** linorobot2 — https://github.com/linorobot/linorobot2
- **Upstream firmware repo:** linorobot2_hardware — https://github.com/linorobot/linorobot2_hardware
  (the microcontroller firmware — `firmware.ino`, `lino_base_config.h`, `pid.cpp` — lives here)
- **Upstream author / copyright:** Copyright (c) 2021 Juan Miguel Jimeno
- **Upstream license:** Apache License, Version 2.0
- **Pinned upstream version:** branch `jazzy`, commit
  `aaf9d59cd18c0cd1905be6fdae9ea5c99961a766` (2026-04-30) — the HEAD of `linorobot2_hardware`
  this overlay was created from. The three derived source files were last modified upstream at
  commit `36ffb76d9a1e0e6d9b31710416ef3bad5b64631f` (2026-04-10, "Add support for ESP32 Wifi")
  and are unchanged through the pinned HEAD. (Verified by diffing the overlay files against the
  full upstream history: the only differences are this robot's own configuration — pins, PID
  gains, geometry, IMU selection — and the OpenAMRobot additions listed below.)

### Files derived from linorobot2 (remain Apache-2.0, modified by OpenAMRobot)

The following files are **modified derivatives** of linorobot2 source. Per Apache-2.0
§4(b), each carries a prominent notice stating it was changed:

| File | Origin | OpenAMRobot modifications (summary) |
|---|---|---|
| `boards/teensy_4_0/linorobot2_overlay/src/firmware.ino` | linorobot2 `firmware.ino` | feedforward + dither, runtime encoder-ripple correction table, `/debug/*` telemetry topics (`/debug/tune`, `/debug/openloop`, `/debug/left`, `/debug/right`), bounded open-loop test mode, IMU workaround (MPU6500), CycloneDDS-oriented tweaks |
| `boards/teensy_4_0/linorobot2_overlay/lib/pid/pid.cpp` | linorobot2 `pid.cpp` | back-calculation anti-windup |
| `boards/teensy_4_0/linorobot2_overlay/config/lino_base_config.h` | linorobot2 `lino_base_config.h` | this unit's pins, PID gains (K_P 2.0 / K_I 0.1 / K_D 0.1), MOTOR2_GAIN, measured drivetrain constants, IMU/board selection |

If you redistribute these files (or your own modifications of them), you must keep their
Apache-2.0 headers, the `Copyright (c) 2021 Juan Miguel Jimeno` notice, and a statement
of your changes, and include a copy of the Apache-2.0 license (`LICENSE-Apache-2.0`).

## Trademarks

The linorobot2 name and its author's name are used only to describe the origin of the
derived files. This does not imply endorsement.
