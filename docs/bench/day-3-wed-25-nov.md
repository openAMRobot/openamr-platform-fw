# Day 3, Wednesday 25 November: wheels-up spin and link-loss x5

Procedure 03 steps 8 to 11. **A wheel turns. Two people. Wheels off the ground. Speed 0.05 m/s only.** Owner: Arka, Evans present or on call.

## Before the first spin (read aloud, both people)
- [ ] Wheel free, nothing within reach of it, stop switch in reach and tested with the motor unpowered.
- [ ] 2000h = 250 saved and read back after a power cycle (from Tuesday), checked again this morning with `zlac_read_faults.py can1`.
- [ ] Wheel diameter value written. Video or tachometer ready. Both names in the record.

## Steps
- [ ] Step 8 spin: `zlac_spin.py can1 --wheels-up-confirmed --wheel-diameter-m D --speed 0.05 --duration 10`, both directions, no fault afterwards.
- [ ] `busload.sh` during the spin, peak recorded.
- [ ] Step 9 unplug variant, run 1 to 5, stop time recorded each run (last host `601` frame to standstill).
- [ ] Step 9 silent-host variant (`kill -9` on the spin script), run 1 to 5.
- [ ] Step 10 control case at 2000h = 0, once, last, stop switch in hand. The wheel is expected to keep turning. Afterwards power cycle and check 2000h is 250 again.
- [ ] Step 11 one safe fault read-out check (no over-voltage).
- [ ] 10 stop times written in one table; compare with the limit Evans sets (`TODO(Evans)`).

## End of day
- [ ] Drive supply off. Logs and video named and saved. Any run that did not stop is reported to Evans the same day.

Stop if: the wheel moves before the script starts, runs faster than expected, or any run does not stop by itself (stop it with the switch, record it, do not repeat until Evans has seen it).
