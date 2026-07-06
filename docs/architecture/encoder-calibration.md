# Encoder ripple calibration

*Applies to the Teensy 4.0 `linorobot2_overlay` firmware.*

The left wheel showed a slow low-speed "oscillation" (a ~1 s, ±6 rpm limit cycle) that barely
responded to PID gains. It was **not** a control-loop problem: the left AS5040 magnet is
off-centre, so the *measured* rpm carries a geometric ripple that the PID was chasing. This page
documents the ripple, the runtime correction table, and why the table is a mitigation rather than
the durable fix.

## The ripple (measured)

An open-loop constant-speed sweep, binning measured rpm by wheel angle (`counts mod CPR`), showed:

- **LEFT wheel:** a **2-cycle-per-revolution, ~40 % peak-to-peak** error (≈ 0.85 → 1.22),
  **identical at 120 / 180 / 250 PWM** → locked to wheel *angle*, not time = a mechanical encoder
  defect (off-centre / tilted AS5040 magnet), not a real speed oscillation.
- **RIGHT wheel:** only ~±4 % (well aligned).

No PID gain can remove an artefact that is in the measurement itself.

## Runtime correction table (`/debug/enc_cal`)

The firmware holds a per-wheel correction table and divides the measured rpm by the table entry at
the current wheel angle:

```
true_rpm = measured_rpm / CAL[bin],   bin = (counts mod CPR) · NBINS / CPR
```

- `ENC_CAL_NBINS = 36` bins, `ENC_CAL_CPR = 1024`. Two tables: `LEFT_CAL[36]`, `RIGHT_CAL[36]`.
- Applied in `calib_rpm()` to `current_rpm1/2` **before** the PID and odometry — instant, no
  averaging, no lag. A table entry ≤ 0.05 is ignored (guard against divide-by-tiny).
- Loaded at runtime via `/debug/enc_cal` (`std_msgs/Float32MultiArray`, 72 floats = 36 left then
  36 right). Until a table is received, both tables default to **1.0 = passthrough** (raw rpm), so
  an un-calibrated boot behaves exactly like no correction.

## Why the table is loaded at runtime, not compiled in

The encoder is read **incrementally**: counts start from 0 at every Teensy boot, at whatever
position the wheel happens to be in. So `counts mod 1024` is an angle **relative to the boot
position**, not an absolute wheel angle. Every reflash reboots the Teensy and shifts the encoder
zero by a random (and different left/right) angle.

A **compiled-in table would therefore be applied at the wrong phase** after every flash. A
compiled table was tried and failed to converge (it even produced an anti-phase result that
*doubled* the ripple). The working approach loads the table at runtime so its phase matches the
current boot's encoder zero.

## Calibration workflow (host-side)

The **shape** of the ripple is fixed (it is the magnet geometry); only its **phase** moves per
boot. So the shape is captured once as a reference, and each boot only re-aligns the phase:

1. A reference table (fixed shape) lives in the host tooling (`scripts/encoder_ref_table.json` in
   the working repo).
2. After **every Teensy power-cycle**, a short alignment run (~6–8 s) spins the wheels, measures
   the raw per-angle ripple, correlates it sub-bin (~1°) against the reference to find the current
   phase, rolls the reference to that phase, and publishes the 72-float table on `/debug/enc_cal`.
3. The table lives in Teensy RAM, so it must be re-sent after a power-cycle — a ROS restart on the
   host does **not** require re-alignment, but a Teensy reboot does.

> ⚠️ **Alignment gotcha:** the alignment routine must flatten the table to 1.0 (passthrough)
> *before* measuring. If it measures with a table already loaded, it reads the residual of the
> loaded table, computes the wrong phase, and produces an **anti-phase** table that *doubles* the
> ripple (~71 %, worse than raw). Symptom: the post-check shows a **larger** ripple than the raw
> baseline.

Result after alignment: LEFT ±40 % → ±4 %, RIGHT ±3.5 %, flat, instant, and it survives a reboot
once re-aligned.

## Honest assessment: table vs velocity filter

The runtime table works, but it is a **per-boot ritual** that is fragile (the phase must be
re-aligned every power-cycle, and the flatten-before-measure gotcha is easy to hit). Two
alternatives were considered:

- A **half-revolution angular velocity filter** (average over 512 counts) cancels the ripple
  cleanly but adds ~0.6 s of lag — rejected for closed-loop control.
- The adopted low-speed estimator (small 12-count window) reduces quantization noise without the
  ripple-cancelling lag, and the table corrects the remaining per-angle error.

**The durable fix is a velocity filter / better encoder mounting, not a position-indexed table:**
a position-indexed table *cannot* be made reliable with an incremental encoder because the phase
is lost at every power-cycle. The runtime table + per-boot re-alignment is the working mitigation
today; treat it as such.

See [control loop](control-loop.md) for where `calib_rpm` sits in the loop, and
[debug telemetry](debug-telemetry.md#debugenc_cal--runtime-encoder-ripple-table-std_msgsmsgfloat32multiarray-reliable)
for the wire format.
