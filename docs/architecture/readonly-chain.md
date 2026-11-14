# Read-only chain on the host: drive emulator to host readiness gate

Integration proof, do not merge. Owner: Evans. First reviewer: Mohamed.

This proof shows that the sprint modules fit together on one host, on simulated
time, and lists every mismatch found between them. It has no command path: the
only frame the controller side can send is an SDO initiate-upload (read)
request built by `co_upload_sched`. There is no `/cmd_vel` input, no target, no
enable, no NMT command and no write.

Status: SIMULATOR-TESTED (host, simulated time). Nothing here is board-verified.

## Chain

```
 ZLAC8015D emulator (tools/can_sil)          recorded candump -L trace
   OD 606Ch:01/02, 6041h, 603Fh, 2026h:05      (same chain, replay mode)
   heartbeat 701h, boot-up on restart                  |
            |  CAN1 model: 500 kbit/s, frame time,      |
            |  one bus, drive response time             |
            v                                           v
 +--------------------------------- oamr_sil_readonly (C, ASan + UBSan) -----+
 |  co_hb_consumer (#49) ---------------------+                               |
 |  co_upload_sched (#49) --> 5 objects, one SDO upload in flight            |
 |      | samples with seq, sample time, status                               |
 |      v                                                                     |
 |  zlac_telemetry (#49): velocity pair, statusword, fault, resolution       |
 |      | both channels VALID and new, else skipped                           |
 |      v                                                                     |
 |  diff_odometry (#48) oamr_odom_update_speeds                              |
 |                                                                            |
 |  tfluna x2, mb7060 x4 + mbseq, icm42688 (fake SPI), daly_rx (#48)          |
 |      |                                                                     |
 |  freshness (#48), one tracker per input                                   |
 |      v                                                                     |
 |  record_model.c (TEST ORACLE): subsystem state, faults, controller state  |
 |  record_json.c: status record every 100 ms, aux record every 200 ms       |
 +-----------------------------------|----------------------------------------+
                                     | JSON lines (stdout), host delay 2 ms
                                     v
 cosim.py: record -> BaseSnapshot / AuxSnapshot (one mapping, see below)
                                     v
 BaseGate of platform-sw #137 (base_status_core.py, imported unchanged from a
 checkout outside this repository)
```

Files: `tests/sil/readonly/` (C program in `src/`, `cosim.py`, `conftest.py`,
`test_readonly_chain.py`, `CMakeLists.txt`). The firmware modules are compiled
from `firmware/` unchanged.

How to run:

```
git clone https://github.com/openAMRobot/openamr-platform-sw /tmp/psw
git -C /tmp/psw fetch origin pull/137/head && git -C /tmp/psw checkout FETCH_HEAD
cmake -S tests/sil/readonly -B build/sil-readonly \
  -DOAMR_GATE_DIR=/tmp/psw/ros2/src/openamrobot_drivers/openamrobot_drivers
cmake --build build/sil-readonly
ctest --test-dir build/sil-readonly --output-on-failure
```

Without `OAMR_GATE_DIR` the pytest scenarios are reported as skipped, never
as passed.

## What was compiled together, at which commits

| Part | Source | Commit |
|---|---|---|
| Base: CANopen read-only client, ZLAC telemetry decoders | platform-fw #49, branch `night-shift/fw2-canopen-ro-2026-11-14` | `25a5c72e` |
| Sensor and telemetry modules (tfluna, mb7060, icm42688, freshness, diff_odometry, daly_rx) | platform-fw #48 | `30374aee` |
| ZLAC8015D emulator, can_trace | branch `night-shift/fw2-can-sil-fix-2026-11-14` (follow-up of platform-fw #39) | `ad4b0237` |
| Host readiness gate `base_status_core.py` | platform-sw #137 (outside checkout, not copied) | `5f278ce7` |
| BaseStatus, BaseSubsystemStatus, BaseFault (field and constant reference) | interfaces #38 | `ca44a258` |
| Toolchains | gcc 13.3.0 and clang 18.1.3, both `-Wall -Wextra -Werror -Wconversion -Wshadow -Wpedantic`, ASan + UBSan; Python 3.12, python-can 4.6.1, pytest 9.2.0 with `-W error` | |

Both merges were clean; no merged file was changed and no build fix was needed.

## Status record and its mapping to BaseStatus (interfaces #38)

The record is a plain C struct printed as one JSON line. It is not a ROS
message. The single mapping table lives in `src/record_json.c` (printed by the
`mapping` command); `cosim.py` turns a record into the `BaseSnapshot` the gate
of #137 takes.

| Record field | BaseStatus field (#38) | Note |
|---|---|---|
| `contract_version` | `contract_version` | 1 = `CONTRACT_VERSION` of #38 |
| `boot_id` | `boot_id` | uint32 |
| `sequence` | `sequence` | +1 per record, from 0 after a controller restart |
| `monotonic_us` | `monotonic_us` | uint64 |
| `state` | `state` | `STATE_*` name; derived by the test oracle |
| `operating_state` | `operating_state` | scenario input standing in for the owner's state machine; not decided here |
| `recovery` | `recovery` | `REQUIRED` while a latched fault is held |
| `faults[]` | `faults[]` (`BaseFault[<=16]`) | `code`, `lifecycle`, `condition_active`, `inhibits_motion` |
| `subsystems[]` | `subsystems[]` (`BaseSubsystemStatus[<=16]`) | `id`→`subsystem`, `state`, `data_age_us`→`data_age` |
| `subsystems[].detail` | none | test-only reason word |
| `wheel1`, `wheel2`, `odom` | none | no odometry or per-wheel validity in BaseStatus |
| `meas` | none | measurements for this document |

Subsystems used: 1 MOTOR_DRIVER, 2 ENCODERS, 3 IMU, 4 BMS, 5..8 RANGE (MB7060
units 0..3), 9 CLIFF_FRONT and 10 CLIFF_REAR (TF-Luna units 0..1).
Fault codes used: 18003 DRIVER_COMM_LOST, 18004 DRIVER_FAULT,
18005 DRIVER_FEEDBACK_INVALID, 18007 IMU_FAULT.

The derivation of subsystem states, faults and controller state in
`src/record_model.c` is a **test oracle**, not production logic. It exists so
the gate has something to judge. The owner's firmware decides these things.

## Scenarios and outcomes at the gate

Each run: 3.5 s simulated time, wheels set to 30 and -30 r/min in the
emulator's object dictionary at start (oracle), Resume requested at 1.5 s
after start. On every run two invariants are checked: every frame sent by the
controller side is an SDO upload request to 601h, and every odometry step
that succeeded had both wheel samples VALID, new, and no older than
`odom_max_age_us`.

| Scenario | Injection | Outcome at the gate |
|---|---|---|
| nominal | none | READY, Resume accepted, unlocked, no blockers |
| one wheel times out | responses for 606Ch:02 dropped from 2.0 s | locked: ENCODERS STALE (`channel2_not_fresh`), 18005; no odometry step after the switch |
| drive heartbeat stops | emulator heartbeat off at 2.0 s | locked: MOTOR_DRIVER STALE, 18003 LATCHED |
| SDO abort | 6041h answered with abort 06020000h | locked: MOTOR_DRIVER STALE (`READ_FAILED`), 18005 |
| late responses | responses delayed 25 ms (> 20 ms SDO timeout) | locked; late answers counted as unsolicited or wrong object |
| duplicated responses | every response sent twice | **gate stays released**: duplicates are counted and dropped and the values stay correct (deviation, see below) |
| late and duplicated | both | locked |
| CAN silent | emulator bus-off at 2.0 s | locked: MOTOR_DRIVER and ENCODERS STALE |
| emulator restarts | `power_cycle()` at 2.0 s, boot-up frame | locked: `drive_restarted`, 18003 LATCHED, odometry reference reset |
| controller restart with latched fault | heartbeat gap gives 18003 LATCHED, then `reboot` (new boot id 777, sequence 0) | gate counts a restart and carries 18003; locked until Resume, which acknowledges 18003 |
| sequence gap | the next record skips 5 numbers | gap counted (`sequence_gaps == 1`), **gate stays released**: finding F-GATE-1, strict xfail test |
| ToF unplugged | TF-Luna unit 1 stops at 2.0 s | locked: CLIFF_REAR STALE, controller DEGRADED |
| ultrasonic no-target and malformed at power-up | unit 0 answers `R1x3` twice then `R123`; unit 1 always `R765` | early Resume refused (RANGE UNKNOWN); later unit 0 OK, unit 1 OK with `no_target`; locked because the refused Resume is not repeated |
| IMU identity mismatch | WHO_AM_I 00h | locked: IMU DEGRADED `wrong_who_am_i`, 18007, FAULTED |
| host clock steps back | host clock −1 s at 2.0 s | locked at once with STATUS_STALE (negative age is never fresh) |
| status stream stops | no record delivered after 2.0 s | STATUS_STALE 0.5 s (±1 step) after the last delivered record |
| retained message only | one status and one aux record delivered | Resume refused (NOT_LIVE, then STALE); locked |
| recorded trace replay | nominal CAN1 trace written with `can_trace`, replayed instead of the emulator | MOTOR_DRIVER OK, wheel 1 raw 300 |

Deviation from "every failure case ends not ready": duplicated responses on
their own are harmless (the scheduler has one request in flight and drops an
answer it is not waiting for), so the test expects the gate to stay released
and checks the values. The forward sequence gap is a real finding (F-GATE-1).

## Mismatches found between the modules

| # | Area | Mismatch | Proposed fix | Owner |
|---|---|---|---|---|
| M1 | time base width | #49 (`co_upload_sched`, `co_hb_consumer`, `zlac_telemetry`) takes `uint64_t now_us`; #48 (`freshness`, `diff_odometry`, `tfluna`/`mb7060`/`icm42688`, `daly_rx`) takes `uint32_t now_us`. BaseStatus `monotonic_us` is uint64. The glue truncates to the low 32 bits for #48 (wrap after about 71.6 min, handled by #48's wrap-safe age, but two clocks to keep consistent) | one `oamr_time_us_t` (uint64) for all modules; #48 freshness keeps its wrap-safe 32-bit age internally | Evans |
| M2 | odometry input | `oamr_odom_update_speeds` takes one `sample_us` for both wheels; #49 delivers the two wheel samples at different times (skew up to 3 ms in this run, one SDO round per object). The glue passes the older stamp | take a pair struct with both stamps (or the #49 `oamr_tlm_velocity_pair_t` directly) and check both ages | Evans |
| M3 | odometry config | `oamr_odom_config_t` requires `counts_per_rev` and `max_count_delta` even on the speed path, which uses neither; the proof sets placeholder values | validate per path, or split speed and count configs | Evans |
| M4 | speed unit | #49 velocity decoder returns raw 606Ch units; their scale (emulator convention 0.1 r/min, D-24 resolution 2026h:05 = 1) is not a #49 output. #48 needs `speed_rad_s_per_unit` from the caller. The same fact is stated in two places | #49 publishes the unit with the resolution check, #48 takes it from there | Evans |
| M5 | validity conventions | three enums for "is this usable": `oamr_tlm_validity_t` (VALID, NOT_READ, READ_FAILED, BAD_SIZE, STALE, ...), `oamr_fresh_state_t` (UNKNOWN, NEVER, STALE, FRESH), sensor status enums (`OAMR_TFL_*`, `OAMR_MB_*`). The UNKNOWN value is 0 in some and absent in `oamr_tlm_validity_t` (VALID is 0) | zero-is-unknown everywhere; one mapping table to `BaseSubsystemStatus.STATE_*` owned by the status publisher | Evans |
| M6 | wheel naming | #49 speaks of channel 1 and 2 (606Ch:01/02); #48 speaks of left and right with signs. Which channel is left is not known (wheels-up test) | named parameter `left_channel`, filled after the wheels-up test | Evans |
| M7 | freshness ownership | #49 decoders carry their own `max_age_us` and STALE; #48 `freshness` repeats the job; the record needs one age per subsystem | one freshness tracker per input in the publisher; decoders report validity of the value only | Evans |
| M8 | restart handling | `co_hb_consumer` counts boot-ups but nothing in #48 or #49 resets the odometry reference on a drive restart; the glue does it | publish a drive-restart event from #49 that the odometry consumes | Evans |
| M9 | missing in #38 | no field for odometry, per-wheel speed or validity; ENCODERS is one subsystem for two wheels, so "one wheel stale" loses which one | odometry on `/odom`; per-wheel detail in a diagnostics topic or a subsystem per wheel | Mohamed (interfaces) |
| M10 | missing in #38 | no fault code for "speed resolution not confirmed" (D-24) or "IMU identity mismatch" as such; the proof uses 18005 and 18007 | decide whether 18005/18007 cover these or append codes | Evans |
| M11 | #137 vs #38 | #137 `Fault` has `active` and `latched` booleans; #38 `BaseFault` has `lifecycle` and `condition_active`. `cosim.py` maps LATCHED and ACTIVE to `active=True` | #137 reads `lifecycle` directly | Mohamed (platform-sw) |
| M12 | #137 vs #38 | #137 `BaseSnapshot.safety_state` takes the `operating_state` names; #38 calls the field `operating_state` | rename in #137 | Mohamed |
| F-GATE-1 | #137 behaviour | a forward sequence gap is counted (`sequence_gaps`) but does not block or latch; the WP expects the gate to end not ready | decide: block until a Resume after a gap, or document that a gap is only a counter | Mohamed |
| F-EMU-1 | emulator test | `test_ctl017_profile_replays_a_recorded_sdo_session` in `tools/can_sil` failed 1 of 4 isolated runs (wall-clock timing). Under `pytest -W error` three tests and one fixture in `tools/can_sil/tests` fail on unclosed-file warnings, the same on the unmerged emulator branch | close files with `with`; give the replay test simulated time | Evans |

No merged file needed a change to build together.

## Rates and budgets measured in the run

10 s nominal run, object list 606Ch:01, 606Ch:02, 6041h, 603Fh, 2026h:05
(5 objects, one SDO upload in flight, 1 ms scheduler tick, 1 ms emulator
response time, 500 kbit/s):

| Quantity | Value |
|---|---|
| Wheel sample refresh period, each wheel | 15.0 ms mean, 15.0 ms max (666 samples) |
| Skew between the two wheel samples of a pair | 3.0 ms max |
| Odometry steps | 666 in 10 s (one per pair, no rejects) |
| CAN1 bus load | 14.9 % (677 frames/s, heartbeat 100 ms included, no bit stuffing) |
| Longest 8-byte frame with worst-case stuffing | 270 µs |
| Worst age inside a status record, MOTOR_DRIVER (heartbeat) | 100 ms |
| ENCODERS | 15 ms |
| IMU | < 0.5 ms (one step) |
| RANGE (four MB7060 in sequence) | 395.5 ms |
| CLIFF (TF-Luna, 100 Hz) | 0.5 ms (one step) |

The 15 ms refresh comes from three round trips per object (request, 1 ms
drive response, scheduler tick) times five objects; it is a property of the
simulation, not of the real drive. The drive response time is a named
parameter, `drive_response_us`, TODO(Evans): measure from a capture.

## What this proof does not cover

- Real CAN timing: arbitration, error frames, bit stuffing on the wire, the
  real drive's SDO response time. The bus model is frame length at 500 kbit/s
  on one bus.
- The STM32 port: no HAL, no interrupts, no DMA, no task timing. Pins and the
  MB7060 trigger pulse are not modelled; the SPI part is a register-file fake.
- Ethernet, UDP, micro-ROS or ROS 2 transport. The host delay is a constant 2 ms.
- Any command path. No /cmd_vel, no target, no enable, no write, no NMT command.
- The owner's state machine: `operating_state` is a scenario input, and the
  controller state, faults and lifecycle come from a test oracle.
- The Daly BMS protocol: decoder present, always UNKNOWN until the protocol of
  the delivered unit is confirmed.
- Hardware facts: track width, wheel radius, speed unit, sign and side of each
  channel, sensor limits. All are named parameters marked TODO(Evans) in
  `cosim.py`.
