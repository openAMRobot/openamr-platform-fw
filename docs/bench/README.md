# Bench week, 23 to 27 November 2026: step-by-step procedures

**Status: proposal for Evans (firmware) and Arka (bench) to adopt, adapt or reject.** Nothing here
has been run on hardware. Every command that exists in this repository was run against the
emulators in `tools/can_sil` on `vcan`; where that was not possible the step says so.

## Scope and gate

P-03 item 30 A8: **23 to 27 Nov is bench work only (Arka). Integrated power-on is 30 Nov to 4 Dec
(Hamed).** Therefore, in this week:

- No test closes the real E-stop chain (PNOZ s3, K1/K2) with the robot assembled. The hardwired
  E-stop is the safety function; firmware and these scripts are not.
- Only bench supplies and single boxes: the NUCLEO-H723ZG, one ZLAC8015D with a hub motor on a
  stand, the Daly BMS, the IMU board, the CTL-017 on a NUCLEO-G474RE. No arms, no lift on the
  robot, no battery pack connected to the robot harness.
- The acceptance pins (the Resume and stop-gate behaviour) are set only after platform-sw #90,
  platform-sw #125, interfaces #35 and ui #50 merge. This week records raw bench numbers only.

## Procedures, in order

| # | File | Bus or box | Moves a wheel or the lift? |
|---|---|---|---|
| 0 | [this page](README.md) | rules, record format, tools | no |
| 1 | [01-nucleo-flash-boot.md](01-nucleo-flash-boot.md) | NUCLEO-H723ZG: flash, boot, LOCKED, IWDG, reset cause | no |
| 2 | [02-usb-link-base-status.md](02-usb-link-base-status.md) | USB link to the Jetson, `/base/status` freshness | no |
| 3 | [03-can1-zlac8015d.md](03-can1-zlac8015d.md) | CAN1 to the ZLAC8015D: termination, bit rate, NMT, 2000h, spin, link loss x5 | **wheel, wheels up** |
| 4 | [04-can2-daly-bms.md](04-can2-daly-bms.md) | CAN2 to the Daly BMS | no |
| 5 | [05-imu.md](05-imu.md) | ICM-42688-P on SPI4 | no |
| 6 | [06-can3-ctl017.md](06-can3-ctl017.md) | CAN3 to the CTL-017 on the G474: heartbeat, homing without load | **lift motor, unloaded** |
| 7 | [07-safety-inputs.md](07-safety-inputs.md) | safety inputs, once Evans assigns the pins | no |

One page per day: [Mon](day-1-mon-23-nov.md), [Tue](day-2-tue-24-nov.md),
[Wed](day-3-wed-25-nov.md), [Thu](day-4-thu-26-nov.md), [Fri](day-5-fri-27-nov.md).
Day plan: Mon 01 and 02; Tue 03 steps 1 to 7 and 05; Wed 03 steps 8 to 11; Thu 04 and 06 steps 1 to 5;
Fri 06 steps 6 and 7, 07 if the pins exist, repeats.

## Where each command comes from

| Marker | Meaning |
|---|---|
| (no marker) | The file exists on this branch (`tools/bench`, `tools/can_sil`, `docs/`). |
| **[pending #22]** | Needs the STM32CubeMX project of platform-fw #22 (`boards/stm32/OpenAMRobot`). |
| **[pending #37/#40]** | Needs the base core and port draft stack (platform-fw #37, then #40). |
| **[pending #29]** | Needs the HIL scaffold of platform-fw #29 (`tools/hil`, `docs/hil-bench.md`). |
| **[pending upperbody-fw #15/#16]** | Needs the CTL-017 firmware drafts of openamr-upperbody-fw. |
| **[pending interfaces #30]** | Needs the `/base/status` message definitions (draft, not merged). |
| **[firmware gap]** | The firmware does not do this yet. The step is written so that it fails visibly until it does. |

Branches stack: #40 is based on #37, #37 on #22. A bench image must come from the tip of that
stack, and the git commit goes in the record.

## Rules that apply to every procedure

1. **Two people present** for any step marked "moves a wheel or the lift": one runs the commands,
   one has a hand on the stop (bench supply switch or the temporary hardwired E-stop). Name both
   in the record.
2. **Wheels off the ground** (both drive wheels, on stands, free to turn, nothing within reach of
   the spinning wheel) for every ZLAC step that turns the motor. The scripts refuse to send motion
   without `--wheels-up-confirmed` and `--wheel-diameter-m`.
3. **Speed cap:** 0.05 m/s for every spin in this week. The scripts cap at 0.1 m/s and refuse
   above 0.2 m/s. Do not use `--allow-high-speed`. The 0.5 m/s ceiling (P-03 item 30 A5) is never
   commanded on the bench.
4. **Power limits.** Every supply is current limited before the first power-on. The values are
   `TODO(Evans)` in each procedure; do not power a box with an unlimited supply. The SAFE bus is
   25.6 to 29.2 V (P-03 item 21), and the ZLAC8015D over-voltage protection is at 77 V (P-03 item
   26): a bench supply above the SAFE range is a test of the drive's protection, not a bench step.
5. **ZLAC8015D inrush.** One drive has 940 uF at its DC input and draws 50 to 60 A at switch-on
   (P-03 item 26). A bench supply with a low current limit will fold back at switch-on. How the
   bench supply is connected (precharge resistor, soft-start, or a supply with enough current) is
   `TODO(Evans)`. Do not defeat the limit to make it start.
6. **Never two CAN masters.** A bus has one master at a time: the host PC adapter *or* the NUCLEO,
   not both. Unplug the other before you start.
7. **Stop on anything unexpected:** a wheel or the lift moving when it was not commanded, smoke,
   smell, a supply in current limit with no explanation, a fault code you cannot name. Cut the
   supply, write down what happened, do not retry until the cause is written down.
8. **A failed step is a result.** Record FAIL with the numbers. Do not edit a limit to make a step
   pass. A limit that is `TODO(owner)` is recorded with the measured value and judged by the owner.

## Host setup (bench PC, Ubuntu 24.04)

```
python3 -m pip install python-can pyserial pytest
sudo apt-get install can-utils stlink-tools openocd gdb-multiarch
# or: arm-none-eabi-gdb from the GNU Arm toolchain used for the build
```

- CAN adapters: type and SocketCAN interface names are `TODO(Arka)`. These procedures call the
  interfaces `can1`, `can2`, `can3` (one per bus). With one adapter, test one bus at a time and
  say so in the record. Bit rates: CAN1 500000, CAN2 250000, CAN3 500000.
- Serial port of the ST-LINK virtual COM port: `ls -l /dev/serial/by-id/`; use the persistent
  path, not `/dev/ttyACM*`. Record the path.
- Dry run of the host side before touching hardware: `python3 -m pytest tools/can_sil/tests -q`
  (the emulator suite; includes the bench scripts) and `tools/bench/can_up.sh can1 500000 --dry-run`.

## Record format (one record per procedure run)

Copy this block into the evidence folder (file name `YYYY-MM-DD-<procedure>-<n>.md`):

```
procedure:            03-can1-zlac8015d   step: 6
date, time (UTC):
people (runs / stop): 
repo commit(s):       platform-fw <sha>  upperbody-fw <sha or n/a>
firmware image:       <file> sha256 <hex>   build: Debug | Release
hardware:             board revision, drive serial/firmware, adapter type, supply and limit set
commands run:         (paste, with the output)
result:               PASS | FAIL | NOT RUN
measured values:      
logs and photos:      (file names; candump logs from `candump -L`)
notes / deviations:
```

Attach the raw `candump -L` log for every CAN step. A record without the commit, the image digest
and the people is not evidence.

## Open values collected here

Every `TODO(owner)` of the seven procedures is listed again at the end of its file. Values that
stop the week if unset: wheel diameter (03), bench supply and inrush method (03), CAN adapter
mapping, stop-time limit (03), and the safety pin assignment (07).
