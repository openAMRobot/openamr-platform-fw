# Sensors and telemetry building blocks (fw2-sensors)

Status: proposal for the owner (Evans). Not wired into the board project yet.

This document describes the hardware-independent C11 modules under
`firmware/sensor_bridge` and `firmware/telemetry`, and the host tests under
`tests/host/sensors`. None of these modules touches a pin, a peripheral
register or an RTOS object. The board port (CubeMX project of platform-fw #22)
feeds them bytes, frames, register values and a microsecond time stamp, and
publishes what they return.

## Rules all modules follow

- **No default for a hardware fact.** Every geometry, range, address, unit,
  limit or timeout that is not in a vendor document is a configuration field.
  An unset field leaves the module NOT_CONFIGURED; it never falls back to a
  guessed value. Open values are marked `TODO(Evans)` in the headers.
- **A missing reading is UNKNOWN, never free space** (I8). Range parsers mark
  "no return", "below minimum" and "above maximum" with `valid = false` and a
  status. A consumer must not turn any of those into "path clear" or "floor
  present".
- **A sample is handed out once.** Parsers return each frame exactly once
  with a sequence number. The IMU driver hands out each valid sample once.
  Nothing returns an old value as new.
- **Time** is a caller-supplied `uint32_t` microsecond clock that wraps every
  2^32 us (about 71.6 min). All age computations use modular arithmetic. An
  age of 2^31 us or more is treated as "in the future" and is STALE.
- **Status enums start at 0 = UNKNOWN**, as I8 requires.

## Modules

| Module | Header | What it does |
|---|---|---|
| TF-Luna parser | `sensor_bridge/include/oamr/tfluna.h` | Byte stream to checksum-validated range frame, with classification |
| MB7060 parser and sequencer | `sensor_bridge/include/oamr/mb7060.h` | `Rnnn<CR>` parser, one sample per trigger; trigger-order and timeout decisions |
| ICM-42688-P driver | `sensor_bridge/include/oamr/icm42688.h` | SPI init, read, scaling; adapted from platform-fw #46 |
| Daly BMS receive decoder | `telemetry/include/oamr/daly_rx.h` | CAN2 receive-only decode, marked unconfirmed |
| Differential odometry | `telemetry/include/oamr/diff_odometry.h` | Twist and pose from wheel speeds or counts |
| Reset cause | `telemetry/include/oamr/reset_cause.h` | RCC_RSR to I8 reset_cause enum (proposal) |
| Freshness | `telemetry/include/oamr/freshness.h` | Age and STALE state per data source |

### TF-Luna (Benewake)

Source: Benewake SJ-PM-TF-Luna A05 Product Manual, Appendix I.

Frame (both "9-byte/cm" (factory default) and "9-byte/mm"):
`0x59 0x59 Dist_L Dist_H Amp_L Amp_H Temp_L Temp_H Checksum`; checksum is the
low 8 bits of the sum of bytes 0 to 7; temperature = Temp/8 - 256 degC.

The unit (cm or mm) must match the format set in the sensor and has no
default in the parser. Classification:

| Status | Condition (manual section) |
|---|---|
| OVEREXPOSED | Amp = 65535 or Amp > 32768 (6.4) |
| NO_RETURN | Dist = 0 (dummy_dist when Amp is below the threshold, 6.8) |
| WEAK | Amp < 100 with a non-zero distance (6.4) |
| BELOW_MIN | distance < 200 mm (20 cm blind zone, 4) |
| ABOVE_MAX | distance > 8000 mm (6.9) |
| OK | anything else; the only status with `valid = true` |

On a checksum error the parser resynchronises on the next `0x59 0x59` inside
the rejected bytes, so a truncated frame does not swallow the frame after it.

Not covered: the 0x5A command protocol and the I2C mode.

### MB7060 (MaxBotix XL-MaxSonar-WR)

Source: MaxBotix XL-MaxSonar-WR/WRC datasheet PD11838z, pin 5 description.

Frame: ASCII `R`, three ASCII digits (range in cm, up to 765), carriage
return. 9600 baud 8N1, RS-232 polarity at logic level, so the UART must
invert RX (the parser sees the inverted bytes).

| Range | Status | valid |
|---|---|---|
| 21 to 764 cm | OK | true |
| <= 20 cm | AT_MIN: object at the 20 cm minimum or closer | false |
| 765 | NO_TARGET: no detection, not free space | false |
| 000 | FAULT | false |
| other lengths, non-digits, > 765 | counted as malformed, no sample | - |

**One sample per trigger.** `oamr_mb_arm()` is called when the trigger pin is
pulsed; the first good frame disarms the parser and any further frame is
counted as unsolicited. A partial frame from before the trigger is dropped.

**Sequencer.** `oamr_mbseq_step()` returns one of WAIT, TRIGGER(sensor) or
TIMED_OUT(sensor). It never drives a pin. The caller pulses the trigger
(at least 20 us high, datasheet pin 4) and reports it with
`oamr_mbseq_triggered()`. The reading timeout and the gap between sensors
have no default; the datasheet gives a 99 ms cycle with serial data at about
94.3 ms after the trigger, which sets a lower bound for the timeout.

### ICM-42688-P (TDK)

Adapted from the driver in platform-fw #46 (not merged). Register facts are
from TDK datasheet DS-000347 and are listed in the header. Changes from #46:

- gyro full scale, accel full scale and ODR are caller parameters with no
  default (#46 hard-coded 2000 dps, 16 g, 200 Hz);
- scaling and temperature conversion are pure functions with tests;
- the sample type lives in the header, so the module builds without the #46
  core headers;
- a NULL SPI hook is rejected before it is called;
- each valid sample is taken once (`oamr_icm_take_sample`).

INT1 data-ready time stamps (`OAMR_ICM_STAMP_INT1`) are preferred. Until INT1
is wired, `oamr_icm_poll()` reads INT_STATUS and marks the time stamp as
`OAMR_ICM_STAMP_POLL`. Axes stay in the sensor frame; the mounting rotation
belongs in `imu_link`.

If #46 merges first, its `firmware/sensor_bridge/icm42688.c` and this
`firmware/sensor_bridge/src/icm42688.c` must be reconciled; this version can
replace it.

### Daly BMS, receive-only

**Protocol not confirmed for the delivered unit.** The decoder never
transmits. Every decoded value carries `confirmed = false` and
`oamr_daly_rx_usable()` stays false until the owner sets
`protocol_confirmed = true` after a CAN capture of the delivered BMS.

- Identifier: 29-bit, `0x18 << 24 | DataID << 16 | host << 8 | bms` for a
  response from the BMS (Daly CAN Communications Protocol V1.0). The two
  addresses have no default.
- Payloads for Data IDs 0x90 to 0x98 follow the public Daly UART/RS485
  protocol V1.2, which uses the same IDs and 8-byte payloads.
- Newer Daly H/K/M/S-series firmware is publicly reported to use a different,
  register-based CAN protocol. The robot's BMS is an M-series unit, so the
  V1.0 layout may not apply. This is why the decoder ships unconfirmed.
- Current is reported as `(raw - 30000) * 0.1 A` with no direction attached,
  because public sources disagree on the sign.
- Other Data IDs are counted (`unknown_id`, `unknown_id_last`), never
  guessed. Frames with a wrong DLC or out-of-range content are counted as
  malformed and do not change the stored values.

### Differential odometry

Pure maths: `v = (vr + vl)/2`, `w = (vr - vl)/track`, midpoint integration,
yaw wrapped to (-pi, pi]. REP 103 axes. Inputs are either raw wheel speeds
times a speed resolution, or 32-bit position counts with wrap-safe deltas.

Wheel radius, track width, counts per revolution, speed resolution, the sign
of each wheel and the input limits are all required. The per-wheel sign is
there because the drive's left/right channel meaning is not settled. It
must come from a wheels-up test, not from the vendor quick start.

Rejected updates (stale, duplicate or out-of-order time stamp, non-finite
value, dt <= 0 or > 1 s, count jump above the limit) never move the pose and
never write a twist. After a jump or a gap the count reference is re-anchored.

### Reset cause (proposal for I8 decision 6)

`oamr_reset_cause_decode(rcc_rsr)` maps the raw STM32H723 RCC_RSR value
(captured before RMVF is written) to the BaseStatus `reset_cause` enum of
interfaces #38: UNKNOWN 0, POWER_ON 1, BROWNOUT 2, WATCHDOG 3, SOFTWARE 4,
PIN 5. Bit positions match RM0468 and the CMSIS header in platform-fw #22.

Priority, most specific first: IWDG1/WWDG1 to WATCHDOG, SFTRST to SOFTWARE,
LPWRRST to UNKNOWN (no I8 value), PORRST to POWER_ON, BORRST to BROWNOUT,
PINRST alone to PIN. Zero, CPU or domain flags alone, or any undocumented bit
give UNKNOWN. The raw value is kept for `reset_flags_raw`. This relies on
the reference-manual statement that internal resets also set PINRSTF, and
power-on also sets BORRSTF; check this on the bench.

### Freshness

`oamr_fresh_classify()` returns UNKNOWN (not configured), NEVER, STALE or
FRESH. A source is STALE when its age reaches the configured limit (no
default). STALE is latched until the next sample, so a clock wrap cannot
make an old sample FRESH again as long as the source is classified at least
once per limit period.

## Building and testing

```
cmake -S tests/host/sensors -B build/host-sensors
cmake --build build/host-sensors
ctest --test-dir build/host-sensors --output-on-failure --no-tests=error
```

The host build uses `-Wall -Wextra -Werror -Wconversion -Wshadow -Wpedantic`
with AddressSanitizer and UndefinedBehaviorSanitizer (turn them off with
`-DOAMR_SANITIZE=OFF`). The tests need only a C compiler and CMake 3.22.

Cortex-M7 compile check (object files only, no link):

```
cmake -S tests/host/sensors -B build/m7-sensors \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/tests/host/sensors/arm-none-eabi-cortex-m7.cmake
cmake --build build/m7-sensors
```

## Integration notes for the board port

- TF-Luna and MB7060 bytes come from UART RX (DMA or IRQ) into
  `oamr_tfl_push` / `oamr_mb_push`. Call `oamr_tfl_flush` after a UART error.
- Stamp each returned sample on receipt and feed a freshness tracker per
  source; publish UNKNOWN/STALE as such.
- Configure the CAN2 filter for the Daly response identifiers using
  `oamr_daly_rx_expected_id()`; do not send requests until the protocol is
  confirmed (requests are outside this module).
- Read RCC_RSR once at start-up, before HAL clears it, then write RMVF.
