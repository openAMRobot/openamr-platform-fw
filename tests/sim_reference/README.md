SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image.

# Simulation reference of the base controller

Status: **unapproved interpretation, not production authority.** Written by an agent under the
platform lead's decision of 15 Nov 2026 (recorded in the pull request that adds this
directory; the firmware owner is asked to acknowledge it there). Board versions of this logic
(STM32H723ZG base controller) stay with the firmware owner. Nothing here is safety evidence.

It is a plain C11 library plus a Linux process that plays the STM32 role on Linux virtual CAN
in openamr-platform-sw's software-in-the-loop layer (`stm32:=simref`).

## Contents

| Part | File | Source it follows |
|---|---|---|
| Operating state machine, version 2 | `src/simref_sm.c` | `docs/safety/spec/state_machine.yaml`, row by row in file order |
| Command watchdog | `src/simref_ctrl.c` | T11, I8 decision 3 (open) |
| Drive link supervision (node 1 heartbeat consumer) | `src/simref_drive.c` | D-24, `docs/architecture/can1-drive-link.md` 4 |
| Boot reads, CiA 402 enable sequence, wheel targets on CAN1 | `src/simref_drive.c` | `can1-drive-link.md` 5.1 (R1 to R7), 5.3 (E1 to E6) |
| Charge request logic (CHARGE_REQUEST, interlock window) | `src/simref_ctrl.c` | rev 5 5.3, 5.6 |
| Lift permit producer and its frame on the lift bus | `src/simref_permit.c` | platform-fw #20, vectors `permit_base_*` |
| Pins, host messages, CAN to events | `src/simref_ctrl.c` | adapter.md 2.1 |
| Linux process | `src/simref_main.c` | |
| All timing values | `include/simref/simref_timing.h` | each one a proposed test value, not approved |
| One option per open conflict | `include/simref/simref_options.h` | C1 to C8, tick semantics |

Hardware pins of the robot (Y32 SAFETY_OK, base E-stop contact, RESET, charger fault, contactor
feedback) arrive on the process's local Unix datagram socket from the simulation (protocol at
the top of `src/simref_main.c`). The process opens only interfaces whose names start with
`vcan` and refuses any other name at start-up (exit code 3) before it opens a socket.

## Guards

- `CMakeLists.txt` fails at configure time with a toolchain file, when cross-compiling, for an
  ARM processor or an ARM compiler (ctest `configure_refuses_arm`; CI also tries the board
  toolchain file).
- Every board target on this branch runs a post-link guard that fails the build when a symbol
  with the prefix `simref_` is in its ELF: STM32H723 (`boards/stm32/OpenAMRobot/CMakeLists.txt`,
  `tools/ci/simref_symbol_guard.cmake`) and every PlatformIO environment of the Teensy overlay
  (`simref_symbol_guard_pio.py`). Negative test: `guard_test/poison_board.cmake` injects a
  `simref_` object into the STM32 build from the command line; the build must fail.
- `cmake/check_no_simref_use.py` fails if any file under `firmware/` or `boards/` names this
  directory or its headers (the guard scripts are the only exception).

## Interpretations (each also commented in the code)

Defaults are the readings the Python model `tools/spec/model.py` uses. Agreement with that
model is consistency between two readings of the same table, not owner approval.

| Conflict | Options | Default (tested) |
|---|---|---|
| C1 drive link timing | `SIMREF_OPT_C1_DRIVE_TIMING_SIDE_B`: 1 side b (2000h 250 ms, 1017h 100, consumer 300 ms), 0 side a (500 ms, 200) | 1 |
| C2 base stopped | `SIMREF_OPT_C2_STOPPED_BY_TWIST`: 1 v and w thresholds (#20), 0 below 1 rpm (rev 5 5.5) | 1 |
| C3 WCM_FAULT in LOCKED | `SIMREF_OPT_C3_WCM_BLOCKS_RUN`: 0 does not block RUN, 1 blocks | 0 |
| C4 Resume rejected in INHIBITED | `SIMREF_OPT_C4_RESUME_REJECT_STAYS_INHIBITED`: 1 stay INHIBITED, 0 go LOCKED | 1 |
| C5 interlock outside a charge attempt | `SIMREF_OPT_C5_INTERLOCK_ONLY_IN_ATTEMPT`: 1 not applicable outside CHARGING and CHARGER_FAULT, 0 latched anywhere | 1 |
| C6 drive fault in CHARGING | `SIMREF_OPT_C6_DRIVE_FAULT_IN_CHARGING_STAYS`: 1 stay and record, 0 go LOCKED | 1 |
| C7 permit carrier | `SIMREF_OPT_C7_DEDICATED_PERMIT_PDO`: 1 dedicated PDO (SIL layout), 0 RPDO1 (not implemented, build error) | 1 |
| C8 approved arm poses | `SIMREF_OPT_C8_STOWED_ONLY`: 1 STOWED, 0 STOWED and CARRY (process default; the adapter uses each vector's list) | 1 |
| Tick: applicability | `SIMREF_OPT_TICK_APPLICABILITY_CURRENT_STATE`: 1 current state (version 2), 0 tick-start state | 1 |
| Tick: order inside "others" | `SIMREF_OPT_TICK_ORDER_STABLE_OTHERS`: 1 arrival order, 0 by event id | 1 |
| Gap: contactor feedback | `SIMREF_OPT_CONTACTOR_MISMATCH_EVENT`: 1 `input_invalid{IMPLAUSIBLE}`, 2 `estop_diag` | 1 |

Other choices where the specification abstracts (raw signals to events):

- The CiA 402 enable sequence starts at the RESET press edge in LOCKED (SAFETY_OK high, E-stop
  released); the release edge (`reset_short`) then enters RUN through Q1 if the drive reached
  Operation Enabled. Nothing enables the drive automatically, after a power cut, a drive
  restart or a restart of the process. After a release that did not lead to RUN, the drive is
  disabled again. Leaving RUN or INHIBITED for any other state disables the drive.
- `run_ok` is SAFETY_OK high, E-stop contact released, host session fresh, BMS ok (held true:
  this mode does not poll the BMS), drive Operation Enabled and inputs valid (pins known,
  contactor agrees, both 606Ch channels fresh).
- `qualified` is SAFETY_OK high (debounced) at the release; `arming_ok` is RESET seen low since
  boot, E-stop released and no charge command. Y32 high to low acts at once; other edges after
  20 ms.
- Contactor feedback is compared with the SAFETY_OK level (the hardwired chain closes it).
- Drive events: heartbeat timeout, drive restart (boot-up frame) and a statusword that left
  Operation Enabled give `drive_comms{COMMS}`; a transition not reached within the limit gives
  `drive_comms{COMMS}`; an SDO abort or a boot-read mismatch gives `drive_comms{CONFIG}`; the
  fault bit gives `drive_fault`. Detail codes are reported in the status.
- A drive found in Operation Enabled at boot of the process gets zero targets and controlword
  06h before anything else.
- The permit producer is told "base fault" in LOCKED and CHARGER_FAULT; arm evidence comes from
  the simulation; wheel evidence is 606Ch with the drive boot count as its session.
- `reset_hold_5s` parks in LOCKED (R1); the reboot happens at the RESET release (boot cause
  SOFTWARE).

## Not covered

- The CTL-017 gate vectors (`permit_ctl_*`) are the lift controller's (STM32G474RE) logic,
  outside the decision; the adapter lists them as OUT_OF_SCOPE and runs them on the model only.
- No owner ruling on any conflict; no microcontroller timing; no board, drive, hardwired stop
  chain or electrical CAN behaviour. Fixtures and simulation are not safety evidence.

## Build and test

```bash
cmake -S tests/sim_reference -B build/simref -G Ninja
cmake --build build/simref
ctest --test-dir build/simref --output-on-failure --no-tests=error
```
