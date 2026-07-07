# Changelog

All notable changes to this repository are documented here. Dates are ISO-8601.

## [Unreleased]

### Added
- Full firmware documentation set: control loop, `/debug/*` telemetry contract, encoder
  ripple calibration, bring-up, flashing, motion safety, and troubleshooting.
- `LICENSE` (MIT for OpenAMRobot original work), `LICENSE-Apache-2.0` (for the
  linorobot2-derived overlay files), and `NOTICE.md` attributing linorobot2.
- Apache-2.0 §4(b) "modified from linorobot2" notices in the three derived overlay files.

### Changed
- Overlay code refreshed to the firmware deployed on the real robot on **2026-06-29**.

### To do before release
- Pin the exact upstream linorobot2 commit/tag the overlay was forked from (see `NOTICE.md`).

## [deployed-2026-06-29] — real-robot firmware snapshot

The overlay files reflect the firmware running on the robot as of 2026-06-29:

- PID gains `K_P 2.0 / K_I 0.1 / K_D 0.1` with feedforward and dither.
- `MOTOR2_GAIN 1.000`.
- Runtime encoder-ripple correction table (hot-loaded; phase re-aligned per boot via the
  host-side `align_enc_cal.py`).
- Back-calculation anti-windup on the PID integral term.
- `/debug/tune`, `/debug/openloop`, `/debug/left`, `/debug/right` telemetry/command topics.
- Bounded open-loop test mode (`ENABLE_POWERED_DEBUG`).
- MPU6500 IMU workaround; publishes `/imu/data_raw` + `/imu/mag`.
