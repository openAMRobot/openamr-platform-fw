# Day 1, Monday 23 November: host, flash, boot, USB link

Bench work only (P-03 item 30 A8). Nothing moves today. Procedures: 01, 02. Owner of the day: Arka, Evans on call for firmware questions.

## Before you start (morning, 30 min)
- [ ] Read `README.md` of this folder: rules, record format, where each command comes from.
- [ ] Bench PC: `python3 -m pip install python-can pyserial pytest`; `can-utils`, `stlink-tools`, `openocd`, `gdb-multiarch` installed.
- [ ] Host dry run: `python3 -m pytest tools/can_sil/tests -q` passes; `tools/bench/can_up.sh can1 500000 --dry-run` prints the commands.
- [ ] Evidence folder created; record template copied; both names written down (who runs, who stops).
- [ ] Collected `TODO(Arka)` values: NUCLEO revision, probe serial, adapter type and interface names, Jetson module and image.

## Procedure 01: flash, boot, LOCKED, IWDG, reset cause
- [ ] Image built from the tip of the stack (#40), commit and sha256 recorded (Debug and Release).
- [ ] Step 1 probe found. Step 3 flashed, `Verified OK`.
- [ ] Step 5: `motion_allowed` = 0, state LOCKED, commissioning incomplete. FAIL if `motion_allowed` is ever 1.
- [ ] Step 6 IWDG (Release image): needs Evans's test hook; if there is none, record NOT RUN.
- [ ] Step 7 reset cause table filled with raw RSR values for power cycle, RESET button, software reset (IWDG row if step 6 ran).

## Procedure 02: USB link and /base/status
- [ ] Agent starts on the persistent serial path. If the micro-ROS client is not in the image, record "no client session" and stop the procedure here.
- [ ] If it is: graph, rate 10 Hz, 10 minute gap check, cable pull, agent kill x5.
- [ ] Zero publishers on `/cmd_vel` throughout.

## End of day
- [ ] Every record has the commit, the image digest and the names.
- [ ] Open questions sent to Evans (list them, do not guess values).
- [ ] Nothing left powered.

Stop and write it down if: `motion_allowed` is 1, or anything publishes on `/cmd_vel`.
