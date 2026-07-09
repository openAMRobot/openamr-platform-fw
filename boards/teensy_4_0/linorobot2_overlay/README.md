# Teensy 4.0 — linorobot2 overlay

Firmware for the **real OpenAMRobot differential base**, running on a **Teensy 4.0**
(NXP i.MX RT1062, Cortex-M7 @ 600 MHz).

## What this is (and is not)

This is **not** the modular custom firmware planned under `firmware/`
(`encoder_reader`, `motor_controller_bridge`, `sensor_bridge`, `battery_monitor`,
`safety_io`). It is an **overlay on [linorobot2](https://github.com/linorobot/linorobot2)
firmware**: only the files that were changed/added for this robot are kept here. The full
linorobot2 firmware tree is the base; you drop these files on top of it.

Going modular later is a clean migration — the mapping below shows which linorobot2
internals already cover each planned module, so they can be split out one at a time.

## Module mapping (Alex's plan → linorobot2 internals)

| Planned module (`firmware/…`) | Where it lives in this overlay / linorobot2 | Status |
|---|---|---|
| `encoder_reader` | linorobot2 `Encoder` lib reading the AS5040 A/B quadrature (pins in `config/lino_base_config.h`) | ✅ covered |
| `motor_controller_bridge` | linorobot2 `Motor` + `Kinematics` + the per-wheel **PID** (`lib/pid/pid.cpp`) → PWM + DIR to the BLDC drivers | ✅ covered |
| `sensor_bridge` | linorobot2 IMU read (MPU-6500) → `/imu/data_raw` + `/imu/mag`; wheel odometry → `/odom/unfiltered` | ✅ covered |
| `safety_io` | 200 ms `/cmd_vel` **watchdog** (motors stop if commands stop) | 🟡 partial (no HW E-stop / safety_io) |
| `battery_monitor` | — | ⏳ not implemented (no voltage telemetry yet) |
| communication bridge | micro-ROS over USB serial (115200), agent on the host (`openamr-platform-sw/openamrobot_drivers`) | ✅ covered |

## My changes vs upstream linorobot2

- **`config/lino_base_config.h`** — this robot's pin map (motors PWM 1/5, FWD 20/6, REV 21/8;
  encoders A/B 14·15 left & 11·12 right; IMU I²C on the Teensy 4.0 **default Wire pins** SDA/SCL
  18/19 — `SDA_PIN`/`SCL_PIN` are left commented out, so 18/19 are the hardware default, not
  `#define`d), geometry
  (`WHEEL_DIAMETER 0.2`, `LR_WHEELS_DISTANCE 0.46`), `MOTOR_OPERATING_VOLTAGE 24`,
  `COUNTS_PER_REV 1024`, `BAUDRATE 115200`, PID gains, and the IMU workaround
  `USE_MPU9250_IMU` (the chip is an **MPU-6500**, not the 6050 on the silkscreen).
- **`lib/pid/pid.cpp`** — added **anti-windup**: upstream had an unbounded integral term
  ("catapult" on saturation). The integral now uses **back-calculation**: when the output
  saturates, the excess is subtracted straight back out of the integral
  (`integral -= (pid − limit) / ki`) so `ki · integral` only ever supplies what is actually
  achievable. Unlike a static clamp or conditional freezing, this *bleeds* the windup out, so a
  long saturated rise no longer overshoots.
- **`src/firmware.ino`** — the 50 Hz control loop (upstream) plus:
  - a **velocity feedforward + PID** controller (the feedforward carries the holding PWM so the
    closed-loop response is speed-independent), a **small-window velocity estimator** and an
    **anti-stiction dither** for clean low-speed / docking motion;
  - **debug telemetry** publishers `/debug/left`, `/debug/right`, `/debug/pwm`
    (best-effort: target/measured rpm, encoder counts, PWM) — what made hardware diagnosis
    possible;
  - an **open-loop test mode** subscriber `/debug/openloop` (bounded fixed PWM on both motors, PID
    bypassed, gated by `ENABLE_POWERED_DEBUG`) — used to prove the motors/encoders independently of
    the closed loop;
  - **live tuning** `/debug/tune` (PID gains, right-wheel scale, feedforward, dither) and a
    **runtime encoder ripple table** `/debug/enc_cal` (per-angle rpm correction loaded without a
    reflash).

> The copies of `config/lino_base_config.h`, `lib/pid/pid.cpp`, and `src/firmware.ino` in this overlay
> are the **deployed 2026-06-29 firmware** (feedforward + dither + runtime ripple-table). The
> documentation in [`docs/`](../../../docs/) is the reference for the interface contract and matches
> this code.

## Build & flash

The overlay needs the linorobot2 firmware base — specifically
[`linorobot2_hardware`](https://github.com/linorobot/linorobot2_hardware) branch `jazzy`,
commit `aaf9d59` (2026-04-30; pinned in [`NOTICE.md`](../../../NOTICE.md)). In that checkout
(PlatformIO), copy these files over the matching paths, then build/flash for the Teensy 4.0
target. The published topics are `/cmd_vel` (in), `/odom/unfiltered`, `/imu/data_raw`, `/imu/mag`,
and the `/debug/*` topics above; the host bridges them with the micro-ROS agent. (The firmware
publishes **raw** IMU; the filtered `/imu/data` is produced by the host EKF/Madgwick pipeline.)

> ⚠️ The Teensy 4.0 is **not 5 V tolerant**. The AS5040 encoders must be powered at **3.3 V**
> (5 V supply drives ~5 V on A/B → over-voltage on the MCU). See the hardware repo
> (`openamr-platform-hw/electrical/sensors`).
