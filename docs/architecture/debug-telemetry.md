# Debug & tuning interface

*Applies to the Teensy 4.0 `linorobot2_overlay` firmware.*

These topics are **additions to upstream linorobot2**, added for this robot. They were essential
to diagnose the drivetrain and remain the live-tuning and commissioning interface. This page is
the single source of truth for the debug interface; other docs link here.

## Published telemetry (Teensy → host)

The debug topic flow is shown below.

> ### 📐 Diagram: Debug / telemetry topic map
> *Figure - the /debug topics between host and Teensy, split by direction.*
>
> **Prompt to generate this diagram (paste to Claude):**
> ```
> Draw a simple two-column topic-flow diagram between HOST and TEENSY.
> - Teensy -> host (telemetry, best-effort): /debug/left (x=target rpm, y=measured rpm, z=raw counts), /debug/right (same), /debug/pwm (x=pwm left, y=pwm right).
> - Host -> Teensy (commands, reliable): /debug/openloop (raw PWM, gated by ENABLE_POWERED_DEBUG - mark it RED 'can move the motors'), /debug/tune (live PID/feedforward/dither gains), /debug/enc_cal (runtime ripple table).
> Group by direction and colour commands vs telemetry differently.
> ```


All three are `geometry_msgs/msg/Vector3`, **BEST_EFFORT** QoS, published at the 50 Hz loop rate:

| Topic | x | y | z |
|---|---|---|---|
| `/debug/left`  | target rpm (LEFT)  | **measured rpm (LEFT, corrected)**  | cumulative encoder counts (LEFT) |
| `/debug/right` | target rpm (RIGHT) | **measured rpm (RIGHT, corrected)** | cumulative encoder counts (RIGHT) |
| `/debug/pwm`   | PWM LEFT | PWM RIGHT | 0 |

> ⚠️ **The `y` field is the *corrected* rpm.** The firmware runs the measured rpm through the
> small-window velocity estimator **and** the runtime ripple table (`calib_rpm`) *before*
> publishing (`current_rpm1 → debug_cur_rpm1 → debug_left_msg.y`). Until an encoder table is
> loaded, the ripple correction is unity passthrough, but the velocity-estimator smoothing is
> always applied. If you need the raw signal, use the `z` counts. See
> [encoder calibration](encoder-calibration.md).

> ⚠️ **BEST_EFFORT QoS:** a subscriber must request best-effort too, otherwise it receives
> nothing:
> ```
> ros2 topic echo /debug/right --qos-reliability best_effort
> ```

## Commands (host → Teensy)

### `/debug/openloop` — raw open-loop PWM (`geometry_msgs/msg/Vector3`, RELIABLE)

Drives a **fixed PWM, bypassing the PID**, for hardware diagnosis (proving a motor/encoder
channel independently of the closed loop).

- **Only the `x` field is used, and it is applied to *both* motors** (motor2 additionally scaled
  by `motor2_gain`). The `y` and `z` fields are ignored.
- Active only when `|x| ≥ 1` **and** a message arrived within the last **300 ms** (its own
  staleness timeout, separate from the `/cmd_vel` watchdog). Otherwise the loop falls back to
  normal control.
- **Validated & bounded**: NaN/Inf are rejected, and the value is clamped to
  `OPENLOOP_PWM_LIMIT = 0.7 · PWM_MAX ≈ ±716` so a stray/huge command cannot slam the motors.
- **Production gate**: driving motors from this path requires the `ENABLE_POWERED_DEBUG` build
  flag (defined in the current commissioning build). In a production image with the flag removed,
  the subscriber still exists (executor count unchanged) but cannot move the motors.

```
# hold both wheels at PWM 200, republished to beat the 300 ms staleness timeout
ros2 topic pub -r 10 /debug/openloop geometry_msgs/msg/Vector3 "{x: 200.0, y: 0.0, z: 0.0}"
```

> ⚠️ A prior note described `/debug/openloop` as `x = left PWM, y = right PWM`. That is **not**
> what the firmware does — `y` is ignored and `x` drives both wheels. Use `motor2_gain` (via
> `/debug/tune`) if you need to bias the right wheel.

### `/debug/tune` — live gain tuning (`geometry_msgs/msg/Twist`, RELIABLE)

Updates the controller in RAM (compiled defaults are unchanged; a reflash restores them).

| Field | Target | Applied when |
|---|---|---|
| `linear.x / y / z` | `K_P / K_I / K_D` (both PIDs) | always |
| `angular.x` | `motor2_gain` (right-wheel PWM scale) | `> 0` |
| `angular.y` | `kff` (feedforward gain, PWM/rpm) | `> 0` |
| `angular.z` | `dither_amp` (anti-stiction dither, PWM) | `≥ 0` |

> ⚠️ A code comment near the declaration lists `angular.z = ff_offset`; the **actual** callback
> uses `angular.z = dither_amp`. `ff_offset` is fixed at its tuned default.

```
# set K_P=2.0 K_I=0.1 K_D=0.1, motor2_gain=1.0, kff=7.87, dither=92 (the compiled defaults)
ros2 topic pub --once /debug/tune geometry_msgs/msg/Twist \
  "{linear: {x: 2.0, y: 0.1, z: 0.1}, angular: {x: 1.0, y: 7.87, z: 92.0}}"
```

> A field guarded by `> 0` is left untouched when sent as `0` (so you can nudge one gain without
> disturbing `motor2_gain`/`kff`); `angular.z` (dither) accepts `0` to disable it.

### `/debug/enc_cal` — runtime encoder ripple table (`std_msgs/msg/Float32MultiArray`, RELIABLE)

72 floats = 36 `LEFT_CAL` bins then 36 `RIGHT_CAL` bins. Loaded into RAM and applied instantly.
Full explanation in [encoder calibration](encoder-calibration.md).

## Diagnostic recipes

- **Motor/encoder health (wheels raised):** publish equal `/debug/openloop` on both wheels and
  compare `/debug/left.y` vs `/debug/right.y` — this is how the motors/encoders were proven
  healthy independently of the closed loop.
- **Step-response tuning:** command a `/cmd_vel` step and record `/debug/left|right` (`x` target,
  `y` measured) at ≥ 0.25 m/s (below that the rpm is too quantized to tune on).
- All debug subscribers are RELIABLE; the telemetry publishers are BEST_EFFORT.
