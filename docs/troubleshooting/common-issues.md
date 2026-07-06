# Troubleshooting

*Applies to the Teensy 4.0 `linorobot2_overlay` firmware.*

Quick index of the failure modes seen during commissioning and where each is explained in full.

## A `/debug/*` echo prints nothing

`/debug/left`, `/debug/right`, `/debug/pwm` are **BEST_EFFORT**. A default (reliable) subscriber
receives nothing. Request best-effort:
```
ros2 topic echo /debug/right --qos-reliability best_effort
```
See [debug telemetry](../architecture/debug-telemetry.md).

## `/debug/openloop` moves both wheels / ignores `y`

By design: the firmware reads only `x` and applies it to **both** motors (motor2 scaled by
`motor2_gain`). `y`/`z` are ignored. To bias the right wheel use `/debug/tune angular.x`. See
[debug telemetry](../architecture/debug-telemetry.md#debugopenloop--raw-open-loop-pwm-geometry_msgsmsgvector3-reliable).

## `/debug/openloop` does nothing at all

- The command is only honoured for **300 ms** — republish it (`ros2 topic pub -r 10 ...`).
- In a **production build** (`ENABLE_POWERED_DEBUG` not defined) the powered path is disabled by
  design. See [motion safety](../safety/motion-safety.md).

## Left wheel "oscillates" at low speed

Not a PID problem — the left AS5040 magnet is off-centre, so the **measured** rpm carries a ~40 %
per-revolution ripple the PID chases. Mitigation is the runtime ripple table; the durable fix is a
velocity filter / better encoder mounting. See
[encoder calibration](../architecture/encoder-calibration.md).

## Ripple got *worse* after calibration

The alignment routine measured with a table already loaded and produced an **anti-phase** table
that doubles the ripple. Flatten the table to 1.0 before measuring, and re-align after every Teensy
power-cycle (the table lives in RAM). See
[encoder calibration](../architecture/encoder-calibration.md).

## Robot judders or stalls at very low speed

You are below the measured velocity floors (linear ~0.04 m/s, angular ~0.15 rad/s). Command above
them; the anti-stiction dither only helps down to ~0.06 m/s. This is stick-slip, not a torque
shortfall. See [motion safety](../safety/motion-safety.md).

## Robot won't reach commanded speed / RPM ceiling looks halved

`MOTOR_OPERATING_VOLTAGE` and `MOTOR_POWER_MAX_VOLTAGE` must both be **24** for the real 24 V
supply, or the computed max RPM is halved. See [control loop](../architecture/control-loop.md).

## Gain edits have no effect

The `teensy40` build uses `config/lino_base_config.h`, not any `dev_config.h`. Live `/debug/tune`
changes are RAM-only and are lost on reflash/reboot. See
[control loop](../architecture/control-loop.md) and [build & flash](../flashing/build-and-flash.md).

## Flashing reports `error writing`

The soft-reboot flash (`-s`) is timing-flaky; the board is already in HalfKay. Retry without `-s`,
or press the physical button and flash with `-w`. See [build & flash](../flashing/build-and-flash.md).

## Host can't see any topics

DDS/domain mismatch — match the robot's CycloneDDS + `ROS_DOMAIN_ID=0`, and confirm the agent baud
is 115200. See [micro-ROS bringup](../bringup/micro-ros-bringup.md).

## Dropped encoder counts / flaky encoders

Check the encoder supply is on the **3.3 V rail** — the Teensy 4.0 is not 5 V tolerant, and a 5 V
supply over-drives the A/B inputs. See [motion safety](../safety/motion-safety.md#hardware-safety-note--encoder-over-voltage).
