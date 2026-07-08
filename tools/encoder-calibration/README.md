# Encoder ripple calibration (host-side tools)

These are the **host-side** scripts that build and load the encoder ripple-correction table the
firmware applies at runtime (`/debug/enc_cal`). They are Python/ROS 2 tools; they run on the host
(the Pi or a dev PC), not on the Teensy.

For the *why* and the full engineering story, read
[`docs/architecture/encoder-calibration.md`](../../docs/architecture/encoder-calibration.md). Short
version below.

## The problem (why this exists)

The LEFT wheel showed a slow low-speed "oscillation" (~1 s, ±6 rpm) that **would not respond to PID
gains**. It turned out **not** to be a control problem at all: the LEFT AS5040 magnet is slightly
off-centre, so the *measured* rpm carries a **~40 % peak-to-peak ripple locked to wheel angle**
(2 cycles/rev, identical at every speed). The RIGHT wheel is fine (~±4 %). No gain can remove an
error that lives in the measurement itself — so the fix is to **divide the measured rpm by a
per-angle correction table** before the PID ever sees it (`true_rpm = measured_rpm / CAL[bin]`).

The correction **shape** is fixed (it is magnet geometry). Only its **phase** moves: the encoder is
incremental, so `counts mod 1024` is an angle relative to wherever the wheel sat at boot — a
different, random offset every power-cycle. That single fact drives the whole design:

- a **compiled-in / static table does not work** (it lands at the wrong phase after every flash — it
  was tried and actually *doubled* the ripple in anti-phase);
- the table must be **loaded at runtime** and **re-aligned to the current boot's phase**.

## Prerequisites (every run)

- **Wheels off the ground** (the scripts spin the motors open-loop).
- **24 V motor power on** and the **micro-ROS agent running** (so `/cmd_vel` and `/debug/enc_cal`
  are live). See [`docs/bringup/micro-ros-bringup.md`](../../docs/bringup/micro-ros-bringup.md).
- All the spin commands require an explicit **`--arm`** — nothing moves a motor without it.
- CycloneDDS on domain 0 (`RMW_IMPLEMENTATION=rmw_cyclonedds_cpp`, `ROS_DOMAIN_ID=0`).

## The two workflows

### 1. Per-boot alignment — the daily ritual (~8 s)

**Run this after every Teensy power-cycle.** It does *not* remeasure the table; it does a short spin
just to find the current **phase**, rolls the stored reference (`encoder_ref_table.json`) to match,
and loads it:

```bash
source /opt/ros/jazzy/setup.bash
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp ROS_DOMAIN_ID=0
python3 align_enc_cal.py --arm 250
```

Result: LEFT ripple drops from ±40 % to **±4 %**, instantly (no averaging, no lag), and it survives
a *ROS* restart — but **not** a Teensy power-cycle, because the table lives in Teensy RAM. Re-run it
after each power-on.

### 2. Full recalibration — only if the magnet is physically disturbed

If the encoder magnet is ever moved/remounted, the reference *shape* is stale. Rebuild it from
scratch (spins at several PWMs, ~1 min) and load it:

```bash
./calibrate_and_apply.sh              # runs encoder_calib.py then apply_enc_cal.py
# or step by step:
python3 encoder_calib.py --arm 120,180,250   # writes /tmp/encoder_calib.json
python3 apply_enc_cal.py /tmp/encoder_calib.json
```

Once you trust the new profile, copy it over `encoder_ref_table.json` so the fast per-boot
alignment (workflow 1) uses the updated shape.

## ⚠️ The one gotcha that bites

The alignment/measurement must run against a **flat (passthrough) table**. If a correction table is
already loaded when you measure, you read the *residual* of that table, compute the wrong phase, and
produce an **anti-phase** table that *doubles* the ripple (~71 %, worse than raw). The scripts flatten
first; if you script your own flow, send `python3 apply_enc_cal.py --flat` before measuring.
Symptom of getting it wrong: the post-check ripple is **larger** than the raw baseline.

## Files

| File | Role |
|---|---|
| `align_enc_cal.py` | **Per-boot** phase alignment (workflow 1). Reads `encoder_ref_table.json`. |
| `encoder_ref_table.json` | The reference ripple **shape** (36 bins/wheel). Regenerate only if the magnet moves. |
| `encoder_calib.py` | **Full** characterization (workflow 2) — measures the ripple across PWMs → `/tmp/encoder_calib.json`. |
| `apply_enc_cal.py` | Publishes a table to the firmware (`/debug/enc_cal`, 72 floats = 36 L + 36 R). `--flat` disables correction. |
| `calibrate_and_apply.sh` | Orchestrates workflow 2 (calibrate → apply). `OPENAMR_WS` optionally sources an overlay. |

## The real fix

This is the **deployed, working** solution — but it is a per-boot ritual, not the end state. The
only durable *hardware* fix is a **centred, untilted AS5040 magnet**, which removes the ripple at
the source. Until the encoder is remounted, run workflow 1 after every power-on.
