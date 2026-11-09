# Day 5, Friday 27 November: lift homing, safety inputs if ready, repeats, handover

Procedures: 06 steps 6 and 7, then 07 only if Evans has assigned the pins, then repeats. **Lift motor unloaded. Two people.** Owner: Arka, Evans on call. Integrated power-on is next week (30 Nov to 4 Dec, Hamed); nothing today closes the E-stop chain.

## Procedure 06 steps 6 and 7: homing and stops (motor unloaded)
- [ ] Two-person checkpoint: motor free, nobody near, supply switch in hand, current limit low, stand-in or real sensors placed.
- [ ] Step 6 homing: ends in IDLE with a valid position. Hall counts and currents written down.
- [ ] Step 6 deliberate failure (target never presented): the node must fault by itself.
- [ ] Step 7 table: STOP, keep-alive loss, core heartbeat loss (within 300 ms plus stopping time), NMT leave, supply off, node power cycle (no motion without a new homing).

## Procedure 07: safety inputs (only if the pin table is filled in and the image is built with it)
- [ ] If not: record NOT RUN and the reason. Do not wire anything.
- [ ] Otherwise: step 2 outputs off at boot, step 3 each input, step 4 state table, step 5 open wires. `motion_allowed` 0 whenever not RUN.

## Repeats and handover (afternoon)
- [ ] Repeat any step that was a FAIL for a wiring reason only. A result that failed on the numbers stays FAIL.
- [ ] One summary page: per procedure PASS, FAIL or NOT RUN, with the record file names.
- [ ] All `TODO(owner)` values measured this week listed for Evans.
- [ ] All logs, photos and video in the evidence folder, with the commit and image digests.
- [ ] Bench powered down, packs disconnected.

Stop if: the motor runs with no command, runs after a fault, or homes in the wrong direction. Cut the supply, record, stop.
