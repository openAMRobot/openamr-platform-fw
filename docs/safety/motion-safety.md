# Motion safety

*Applies to the Teensy 4.0 `linorobot2_overlay` firmware.*

Firmware can drive the motors directly, so the safety-relevant behaviours are documented here.
This is the current commissioning state; it is **not** a substitute for a hardware E-stop (none is
wired yet — see the [overlay module mapping](../../boards/teensy_4_0/linorobot2_overlay/README.md)).

## Command watchdog (`/cmd_vel` timeout)

If no fresh `/cmd_vel` arrives within **200 ms**, `moveBase()` performs a **deterministic full
stop**: it brakes all motors and resets the PIDs (not merely "set velocity to 0 with the PID still
running"). Odometry keeps updating. This is the primary software safety net — it works even if the
publisher dies or the network drops.

## Open-loop diagnostic bounds

The raw-PWM `/debug/openloop` path bypasses the PID and can move the motors, so it is fenced:

- **Input validation** — NaN/Inf are rejected (treated as PWM 0).
- **Magnitude clamp** — the value is clamped to `OPENLOOP_PWM_LIMIT = 0.7 · PWM_MAX ≈ ±716`, so a
  stray or oversized command cannot slam the motors at full PWM.
- **Staleness timeout** — the command is only honoured for **300 ms**; it must be republished to
  keep driving, so a dropped publisher stops the wheels.
- **Production gate** — powered raw debug requires the `ENABLE_POWERED_DEBUG` build flag. Remove it
  for a production image and `/debug/openloop` can no longer move the motors (the subscriber stays,
  so the executor layout is unchanged).

See [debug telemetry](../architecture/debug-telemetry.md) for the full interface.

## Startup / agent-loss behaviour

- The control loop only runs while the micro-ROS agent is connected. On **agent disconnect** the
  firmware calls `fullStop()` and tears down its ROS entities, then waits to reconnect.
- IMU or magnetometer init failure is treated as **fatal**: the firmware blinks the LED and does
  not run the control loop (it never silently drives with a dead IMU). See
  [micro-ROS bringup](../bringup/micro-ros-bringup.md) for the LED blink codes.

## Measured velocity floors (operating limits)

Closed-loop sweep on the ground (under load, PID + dither, like docking), 2026-07-02:

| Axis | Stalls | Judders | Reliable floor | Clean floor |
|---|---|---|---|---|
| Linear  | ≤ 0.02 m/s | ~0.03 m/s | **0.04 m/s** | 0.05 m/s |
| Angular | ≤ 0.08 rad/s | ~0.10–0.12 rad/s | **0.15 rad/s** | 0.20 rad/s |

- Keep commanded speeds **above** these floors (docking tapers are floored at 0.05 m/s, rotation at
  0.17 rad/s). Below the floor the wheels judder or stall from stick-slip.
- **The motor is well-sized, not under-torqued.** Above the floors the real/commanded velocity
  ratio is ~1.0. The floors are **static friction (stick-slip) + coarse Hall commutation at low
  RPM** (an operating-point limit), not a torque shortfall. The anti-stiction dither (see
  [control loop](../architecture/control-loop.md)) is what pushes the clean floor down toward
  ~0.06 m/s for docking.

## Hardware safety note — encoder over-voltage

The Teensy 4.0 is **not 5 V tolerant** (3.3 V max). The AS5040 encoders accept 3.3–5 V, so they
**must be powered from the 3.3 V rail**: a 5 V supply drove ~4–5 V on the A/B lines, forcing the
Teensy's internal clamp diodes to conduct (out of spec — slow pin degradation, possible dropped
counts). This was corrected on 2026-06-19 by moving the encoder supply to 3.3 V (A/B now cap at
~3.3 V, clean counting). If the 3.3 V rail ever browns out under load, the fallback is a series
resistor (~1–2.2 kΩ) or divider per A/B line, or a level-shifter. Wiring details live in the
hardware repo (`openamr-platform-hw`).
