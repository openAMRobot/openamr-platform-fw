# 07 Safety inputs on the NUCLEO-H723ZG (once Evans assigns the pins)

**Proves:** each safety input reaches the firmware with the right polarity, the safety core
(E-01 rev 3, secondary observe-and-inhibit layer) follows its state table on the bench, and an
open wire or a missing input never reads as safe.
**Moves anything?** No. Signals are simulated with switches and a bench supply. **The real E-stop
chain (PNOZ s3, K1, K2) is not closed this week** (P-03 item 30 A8): the firmware is never the
safety function, and the hardwired chain is tested at the integrated power-on (30 Nov to 4 Dec, Hamed).

> **Blocked until Evans assigns the pins.** In the draft (platform-fw #40, `Port/Inc/port_board.h`)
> every safety pin is `PORT_PIN_UNASSIGNED`: SAFETY_OK, ESTOP_BASE, RESET_PRESSED, WCM_FAULT, RESET_LAMP,
> CHARGE_REQUEST. The pin map lists them as TBD. **Do not start this procedure before the table in step 1 is filled in
> and the firmware image is built with those pins.** Until then the safety core runs with unassigned
> inputs and stays LOCKED, which is what procedure 01 checks. The second E-stop (read by the CTL-017 over CAN3) is
> a separate input: its path is the CAN3 status of procedure 06, and a firmware hook for it is also a `TODO(Evans)` in the draft.

## Preconditions

- Image with the pins assigned, commit and digest recorded. Debug build for the gdb reads.
- Pin assignments written in step 1 and agreed by Evans **before** wiring.
- Bench wiring only:
  - Logic-level signals at 3.3 V from switches or a breadboard with the pull-ups as specified.
  - SAFETY_OK arrives through the ISO1212 isolated input (the PNOZ Y32 output is 24 V on the field side). On the bench,
    drive the ISO1212 field input from a **current-limited 24 V bench supply through a switch**, never from the robot, never from the PNOZ. Current limit `TODO(Evans)`.
  - RESET_LAMP and CHARGE_REQUEST are TPL7407L outputs. Use a lamp or an LED with a resistor
    as the load, **not the real KCHG coil and not the real RESET lamp harness** (the coil
    would close the charging path or open the PNOZ supply; no robot is connected, but keep the
    habit). Load and supply for the driver: `TODO(Evans)`.
- No CAN connection to other nodes in this procedure (the CTL-017 second E-stop is checked elsewhere).
- One person plus a second reading the record is enough; nothing can move.

## Step 1. Pin and polarity table (fill in before anything else)

| Signal | Direction | Pin | Polarity at the pin when the named condition is present | Source |
|---|---|---|---|---|
| SAFETY_OK | in | `TODO(Evans)` | Y32 high = PNOZ enabled. **The ISO1212 output may be inverted by its thresholds: measure it** | PNOZ Y32 via ISO1212 |
| ESTOP_BASE | in | `TODO(Evans)` | spare NO contact to GND, 3.3 V pull-up: pressed = LOW (diagnostic) | base E-stop |
| RESET_PRESSED | in | `TODO(Evans)` | NC contact, 3.3 V pull-up: closed at rest (LOW), pressed or **open wire** = HIGH | RESET button |
| WCM_FAULT | in | `TODO(Evans)` | open collector, 3.3 V pull-up: fault = LOW (TZBOT output polarity to be confirmed) | charger |
| RESET_LAMP | out | `TODO(Evans)` | HIGH = lamp on (lamp on in LOCKED) | TPL7407L |
| CHARGE_REQUEST | out | `TODO(Evans)` | HIGH = request on | TPL7407L |

The polarity rows are from the I8 and E-01 documents written for the earlier controller (the signal names and
logic carry over to the NUCLEO; the pins do not). Confirm each against the wire list of the delivered
harness. The same table goes into the record.

## Step 2. Idle levels and outputs off at boot

Before the firmware runs (hold RESET on the NUCLEO), and after release, measure each output pin with a
multimeter or scope. Expected: **RESET_LAMP and CHARGE_REQUEST are low (off) from power-up** until the
safety core drives them (the draft writes both off at start of `port_app_init`). Brief glitches at
reset: capture with a scope on CHARGE_REQUEST and record the worst; a glitch on the charge request
must not close a contactor (`TODO(Evans)` threshold). With the inputs idle read them with gdb (procedure 01, step 5).

## Step 3. Each input, one at a time

The firmware reads the pins in `read_safety_inputs()` and gives `s_sc_in` to the safety core.
In gdb: `print s_sc_in` (variable names of the image; `info variables s_sc_in`). For each row set the
input, read, and release:

| Input action | Expected in `s_sc_in` |
|---|---|
| SAFETY_OK switch on / off | `safety_ok` 1 / 0, as the switch (confirms the ISO1212 polarity) |
| ESTOP_BASE switch closed (pressed) | `estop_base_active` 1 |
| RESET NC contact opened | `reset_pressed` 1; with the wire unplugged also 1 (open wire reads as held, never as released) |
| WCM_FAULT pulled low | `wcm_fault` 1 |

Pass: each flag follows its input with the polarity of step 1. FAIL: record the pin voltage and the flag.

## Step 4. State table on the bench

Drive the safety core by the switches and watch `s_safety.out` in gdb (`state`: 0 LOCKED, 1 RUN, 2 CHARGING,
3 CHARGER_FAULT; `motion_allowed`; `reset_lamp`; `charge_request`; `reasons`). The **host side** (software Stop,
heartbeat) is injected through the debugger because the host link is not in the bench loop yet:
`set s_sw_stop = 0`, and `call port_on_host_heartbeat()` each 200 ms is not practical by hand: either use a gdb
`while` loop or accept that comms-lost reason (bit 8, 0x100) stays and look at the other bits. How to inject
`TODO(Evans)` (a test hook in the image is better). The commissioning record must also be complete for RUN: until every
field is set (`s_commissioning_missing` = 0) the core stays LOCKED by design; **for this procedure Evans must
provide an image with the record filled for the bench** (values `TODO(Evans)`, for example the
`PORT_COMM_*` fields of `port_commissioning.h`), otherwise only the LOCKED rows can be tested.

Rows (E-01 rev 3 as implemented in the draft safety core; reasons are bits of `OAMR_SC_R_*` in
`safety_core.h`):

| Start | Action | Expected |
|---|---|---|
| power-on | none | LOCKED, lamp on, `motion_allowed` 0 |
| LOCKED, E-stop and SAFETY_OK as for a released chain | RESET short press (under 2 s) and release, SAFETY_OK rises within 1 s | RUN, lamp off, `motion_allowed` 1 |
| LOCKED | RESET short press, SAFETY_OK does **not** rise | stays LOCKED, reason PNOZ did not enable (bit 5, 0x20) |
| RUN | SAFETY_OK drops | LOCKED at once, reason HW safety not enabled (bit 1, 0x2), `motion_allowed` 0 |
| RUN | ESTOP_BASE pressed | LOCKED, reason E-stop diagnostic (bit 0) |
| RUN | host heartbeat stops (500 ms), no inject | LOCKED or inhibited, reason comms lost (bit 8) |
| RUN | software Stop asserted | LOCKED, reason software stop (bit 10) |
| LOCKED | RESET held between 2 and 5 s | ignored |
| any | RESET held 5 s or more | reboot request set (bit 6 reason), then the controller reset about 2 s later (the reset cause is software: check procedure 01 step 7) |
| CHARGING | WCM_FAULT low | CHARGER_FAULT latched, `charge_request` 0 |
| CHARGING | SAFETY_OK high while CHARGE_REQUEST is on for more than 500 ms | CHARGER_FAULT, interlock latch |

Pass: every row as written. A row that differs is a finding for Evans (the draft says it is a
proposal to adopt or rewrite, and the interpretation notes in `safety_core.h` list what to confirm).
**`motion_allowed` must be 0 whenever the state is not RUN, in every row**: FAIL otherwise.
Check the debounce: a 10 ms pulse on SAFETY_OK or RESET must not change the state; a 30 ms one
may (20 ms debounce).

## Step 5. Open wire and loss of supply

| Fault | Expected |
|---|---|
| SAFETY_OK wire unplugged | reads not enabled: LOCKED (pull-down, a broken wire reads not enabled) |
| RESET wire unplugged | reads pressed (held): never RUN by itself |
| ESTOP_BASE wire unplugged | reads released (diagnostic only; acceptable only because the hardwired chain is the safety function): record |
| WCM_FAULT wire unplugged | reads no fault, cannot be detected (accepted in the design questions, record only) |
| 3.3 V pull-up supply removed (if possible) | no input reads as safe |
| MCU halted (debugger stop) | RESET_LAMP output state: record (the lamp goes dark if the MCU halts, a known weakness of the design). With the Debug build the IWDG is frozen while halted: this does not test the watchdog. |

## Pass criteria

Step 3 polarity all correct; step 4 rows as written with `motion_allowed` 0 outside RUN; step 5
rows as written. Any row that disagrees with E-01 rev 3 is recorded, not "fixed" on the bench.

## Stop condition

An output energises a real coil, contactor or lamp harness: disconnect and record. A supply above
what the ISO1212 field input is rated for (`TODO(Evans)`): stop.

## Record

The filled table of step 1, image digest, gdb outputs per row, scope captures of step 2, photos of the wiring.

## Open values (TODO)

- `TODO(Evans)` all six pins and polarities; ISO1212 polarity (measure); the commissioning values for a bench RUN image; a way to inject the host heartbeat and software Stop; output glitch threshold; field-side supply limit for the ISO1212.
- `TODO(Evans)` how the CTL-017 second E-stop enters the safety core (path over CAN3, hook missing in the draft).
