# Day 4, Thursday 26 November: Daly BMS and CTL-017 up to homing

Procedures: 04 and 06 steps 1 to 5. No wheel today; the lift motor does not turn today. Owner: Arka, Evans on call.

## Procedure 04: Daly BMS (CAN2, 250 kbit/s)
- [ ] BMS order code and firmware recorded. Pack on a non-flammable surface, nobody leaves the room with it connected. No charger, no load, no robot harness.
- [ ] Step 1 termination 55 to 65 ohm. Step 2 `sudo tools/bench/can_up.sh can2 250000`.
- [ ] Step 3 listen 10 s, then request `18900140#0000000000000000`. **Is the answer `18904001`?** If not, the protocol is not V1.0: save raw frames and stop the NUCLEO steps.
- [ ] Data IDs 0x91, 0x92, 0x93, 0x98 answered. Step 4 values against the meter.
- [ ] Steps 5 and 6 (NUCLEO asks, stale and recovery) only if the protocol matches.

## Procedure 06 steps 1 to 5: CTL-017
- [ ] Termination on CAN3, `sudo tools/bench/can_up.sh can3 500000`.
- [ ] Step 1 `nmt_scan.py can3 --expect 0x10 --expect-period-ms 100` PASS; no status frames in pre-operational.
- [ ] Step 2 `cansend can3 000#0110`; status frames with an incrementing counter.
- [ ] Step 3 three stop sensors read on the right bits; outputs measured with a meter only.
- [ ] Step 4 command without core heartbeat: record refused or accepted, with the result byte.
- [ ] Step 5 generators tested: `cangen` for 0x701 and 0x210 (motor supply still off).

## End of day
- [ ] Logs saved. Questions to Evans: Daly protocol result, step 4 result.

Stop if: any heating or smell from the pack, a voltage mismatch above 1 percent, or the lift motor turns with the supply off.
