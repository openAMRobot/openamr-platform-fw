# ZLAC8015D link-loss test (wheels up)

**Proves:** the drive stops its wheels by itself when the host stops sending, using object 2000h
(communication power-down protection time). With the factory value 0 the drive keeps the last
speed. This is the G4 proof that a host crash, a CAN fault or a micro-ROS reset does not leave
the wheels running.

## Acceptance (roadmap risk 2)

CAN1 unplugged at low speed with the wheels off the ground: the wheels stop within the set
time (2000h plus the deceleration set in 6084h). The test is repeated 5 times and all 5 must
pass. The acceptance limit in milliseconds is `TODO(Evans)`; P-03 gives only the 200 to 300 ms
setting of 2000h, not a stop-time limit.

## Preconditions

- Both drive wheels **off the ground** on stands, nobody near the wheels, hardwired E-stop within reach and tested.
- `tools/bench/can_up.sh can1 500000` done, `nmt_scan.py can1 --expect 1` passes, `zlac_read_faults.py can1` passes.
- 2000h set and saved: `zlac_set_link_loss.py can1 --timeout-ms 250 --save-confirmed` passes.
- A way to time the stop: video at 60 fps or more, or a tachometer or scope on the encoder or wheel, plus `candump -ta can1 > run.log`.

## Procedure

1. Start `candump -ta can1 > run.log` in a second terminal.
2. Start the load:
   `zlac_spin.py can1 --wheels-up-confirmed --wheel-diameter-m D --speed 0.05 --duration 60`
   (D is the measured drive wheel diameter in metres; the script caps speed at 0.1 m/s and
   refuses anything above 0.2 m/s unless `--allow-high-speed` is given. Do not use that flag here). Wait until the wheels turn steadily.
3. **Unplug the CAN1 connector** at the base controller side. Note the time on the video or scope.
4. Observe the wheels. They must coast or brake to a stop without any new command.
5. Stop the script (Ctrl-C). Plug CAN1 back in. Run `zlac_read_faults.py can1`. Record any fault.
6. Repeat until there are 5 passing runs in a row (one fail restarts the count after the cause is found and recorded). Then repeat the test once with 2000h = 0 (drive default, do not save) and record that the wheels keep turning, so the test can tell the two cases apart.

## Measure

- t0: last host frame on the bus (`run.log`, last frame with ID 0x601 before the gap).
- t1: wheels at standstill (video or scope).
- Stop time = t1 - t0.

## Pass criterion

- With 2000h = 250 ms: wheels at standstill within **250 ms plus the deceleration time** set in 6084h. Record the measured value; the acceptance limit is `TODO(Evans)` (P-03 gives the 200 to 300 ms setting, not a stop-time limit).
- With 2000h = 0: wheels keep the commanded speed after the unplug (this confirms the test detects the missing protection).
- All 5 repeats pass (the roadmap acceptance). No drive fault left that blocks the next enable.

## If it fails

Do not run any ground test. Check that 2000h was saved (2010h) and survived a drive power cycle, check what the drive counts as communication (`TODO(Evans)`: confirm with the vendor), and report the numbers.
