# 05 IMU: ICM-42688-P on SPI4

**Proves:** the TDK EV_ICM-42688-P board answers on SPI4, WHO_AM_I reads 0x47, the configuration
reads back, samples arrive and have the right signs and magnitudes with the board held by hand.
**Moves anything?** No. The board is turned by hand on the bench.

Facts (P-03 item 26 and decision D5): SPI4 Mode 0 (CPOL low, CPHA first edge), 8-bit, prescaler 8
(6 Mbit/s in the .ioc; the part allows at most 8 MHz); SCK PE12, MISO PE13, MOSI PE14, CS PE11
(software chip select, idle high). INT1 is not assigned to a pin yet, so the draft **polls** the
data-ready flag. The ICM-42688-P WHO_AM_I register is 0x75 and its value is **0x47**.

## Preconditions

- IMU board wired directly to the NUCLEO (no click shield). Board connector CN1, per the wiring list
  of P-03 item 26: pin 16 SCLK, 18 SDI, 20 SDO, 4 CS, 3 INT1, 11 and 13 GND, 19 VIN at 5 V. NUCLEO side:
  PE12 (CN10-26), PE13 (CN10-10), PE14 (CN10-8), PE11 (CN10-6). **INT1 is left unconnected** or
  connected to a free pin that the firmware does not use (`TODO(Evans)`: which pin carries INT1).
- Jumpers: JP2 1-2 (VDD 3.0 V) and JP1 1-2 (VDDIO equals VDD), so the board's I/O runs at 3.0 V and
  the STM32 3.3 V signals stay inside the VDDIO + 0.5 V rating (P-03 item 26). **Check both jumpers
  with a multimeter before power-on**, and read VDDIO with the meter at the first power-on: 3.0 V expected.
- Wires as short as practical, 100 mm or less as a guide (jumpers on the bench, harness CTL-012 on the
  robot); length limit `TODO(Evans)`.
- One person is enough. NUCLEO powered from the ST-LINK USB cable only; nothing else connected.

## Step 1. Continuity and power

Power off: check the four signal wires end to end, GND, and VIN to the 5 V pin of the NUCLEO. Power on.
Measure VIN (about 5 V) and VDDIO (about 3.0 V). Pass: both within 5 percent. FAIL: power off, fix.

## Step 2. WHO_AM_I

The draft checks WHO_AM_I in `oamr_icm_init()` (file `firmware/sensor_bridge/src/icm42688.c`) and
keeps the result in `s_imu_result` (`OAMR_ICM_OK` is 0, `BUS_ERROR` 1, `WRONG_ID` 2, `CONFIG_MISMATCH` 3)
and the value in `s_imu.who_am_i`.

With the debugger (procedure 01 step 5):

```
(gdb) print s_imu_result
(gdb) print/x s_imu.who_am_i
(gdb) print s_imu.configured
```

Expected: `s_imu_result = OAMR_ICM_OK`, `who_am_i = 0x47`, `configured = 1`. Record the raw output.
With the HIL scaffold **[pending #29]**: `python3 tools/hil/imu_whoami.py` reads the boot banner log
(`OPENAMR IMU whoami=0x47`, **[firmware gap]**: the banner does not exist yet).

Failure map: `BUS_ERROR` (all 0x00 or all 0xFF read): wiring, power, chip select or
wrong SPI mode; check with a scope or logic analyser on SCK, CS and MISO (CS low for the whole
transfer, clock idle low, data changes on the falling edge, sampled on the rising edge for Mode 0). `WRONG_ID`: another
device or a different part: record the value read. `CONFIG_MISMATCH`: the configuration write did not read back:
record which register (`TODO(Evans)`) and send the raw log.

## Step 3. Samples arrive

```
(gdb) print s_imu.counters
(gdb) print s_imu.latest
```

Continue and interrupt twice, 1 s apart. Expected: `counters.samples_ok` rises; `read_failures` and
`invalid_samples` stay at 0. The draft configures 200 Hz output rate and the widest ranges
(2000 dps, 16 g; `icm42688.c` of #40, final values `TODO(Evans)`). Compute the rate from the
`samples_ok` difference over the time between two stops (gdb stop times are rough: use a 10 s gap for
a usable number). Pass: rate within 10 percent of 200 Hz. Note that the draft polls INT1's data-ready bit
from the background loop, so the rate and the timestamps depend on the loop time (the draft stamps
with the poll time, not the INT1 edge). Timestamp quality is `TODO(Evans)` and is a known limit.

## Step 4. Signs and magnitudes by hand

Hold the board still on the bench, then rotate it by hand. With `s_imu.latest` read in gdb after each
pose (or the topic `/imu/data_raw` once procedure 02 works **[firmware gap]**):

| Pose | Expected |
|---|---|
| Flat, still, any orientation | accel magnitude about 9.81 m/s2 (1 g) within 5 percent; gyro near 0 (offset below a few deg/s, limit `TODO(Evans)`) |
| Each of the 6 faces up in turn | 1 g on one axis, sign flips between opposite faces |
| Yaw by hand, slow, about the vertical axis | gyro z moves, with a sign that agrees with right-hand rule (counter-clockwise seen from above is positive) |
| Roll and pitch likewise | gyro x and y move on the right axes |

The mounting: the board is mounted with the chip +Y axis forward, the chip at `imu_link`, and the
URDF yaw of minus 90 degrees stays (P-03 item 26). **Evans and Cuma check the orientation on the
delivered board**; this step provides the evidence: for a robot-frame reading (x forward, y left,
z up), record which chip axis moved for each hand rotation and send the table. Any mismatch goes
into one update of the register, the URDF and the model (P-03 item 26). The draft sample
is already in SI units (`gyro_rad_s`, `accel_m_s2`, `temperature_c` in `oamr_imu_sample_t`), which
is what `/imu/data_raw` needs; read them in gdb as such.

## Step 5. Stability

Leave the board still on the bench for 5 minutes. Record the gyro mean and the accel mean on each axis
(a short gdb loop or the host topic). Pass: no jumps, no stuck value (a stuck sample counter is a FAIL
because the freshness rule depends on it: the draft counts a sample "healthy" only with a valid sample
within a maximum age and fewer than a maximum number of consecutive bad reads, `TODO(Evans)`).

## Step 6. Disconnect test

Pull the MISO wire for a moment while sampling. Expected: bad-read counters rise, health goes to
not healthy, and recovers when reconnected. Record the counter values.
(`oamr_icm_healthy`; the draft thresholds are `TODO(Evans)`.)

## Pass criteria

WHO_AM_I 0x47 and configuration read back; sample rate within 10 percent; 1 g on a still board; signs agree
with the table; health reacts to a pulled wire.

## Stop condition

VDDIO above 3.3 V or the board warm to the touch: power off, check the jumpers.

## Record

Board and jumper photos, VIN and VDDIO readings, the gdb output of steps 2 and 3, the pose table,
the 5 minute means.

## Open values (TODO)

- `TODO(Evans)` INT1 pin, final data rate, filter and full scale ranges (draft 200 Hz, 2000 dps, 16 g), gyro offset limit, health thresholds, wire length limit, whether timestamps use the poll time or INT1.
- `TODO(Evans)` and `TODO(Cuma)` orientation of the delivered board against the URDF.
