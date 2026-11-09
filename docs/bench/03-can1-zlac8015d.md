# 03 CAN1 to the ZLAC8015D: termination, bit rate, NMT, 2000h, spin, link loss x5

**Proves:** CAN1 is terminated and runs at 500 kbit/s, the drive (node 1) is alive, object 2000h
(communication power-down protection time) is set to 200 to 300 ms, read back and saved, a wheel
turns at 0.05 m/s with the wheels up, and the wheels stop by themselves when the host goes silent,
5 times out of 5 (roadmap risk 2). This is gate G4.
**Moves a wheel?** **Yes, from step 8.** Wheels up. Two people.

Background: 2000h defaults to 0, which keeps the last speed if the controller goes silent
(P-03 item 30 A4, D-24). The existing scripts in `tools/bench` implement every step; the README
there lists them. All commands below were run against the ZLAC8015D emulator on `vcan` (the
`tools/can_sil` suite) and behave as written; the real drive may differ, and every difference is a
finding to record.

## Preconditions

- Hub motor on a stand, **wheel free to turn, nothing within reach of the wheel**. For this week one
  drive and one hub motor are enough. Wheel diameter, **measured**: `TODO(Evans)` (no default; the
  scripts refuse to move without it). Write the value and how it was measured (tape, calliper).
- Hardwired stop within reach of the second person: the bench supply switch, or the temporary
  E-stop if one is fitted. Tested with the motor unpowered before the first power-on.
- Bench supply for the drive, voltage inside the SAFE range 25.6 to 29.2 V, **current limit set**:
  `TODO(Evans)` (the drive draws 50 to 60 A for a moment at switch-on through 940 uF, P-03 item
  26; how the bench supply is connected for this, with precharge or a soft start, is
  `TODO(Evans)`). Hub motor brake (if the motor has one): released with 24 V, `TODO(Evans)` how it is
  powered on the bench; a locked brake is not a drive fault.
- ZLAC8015D wiring to the CAN adapter: CAN_H, CAN_L, and a common reference as the drive manual
  requires (`TODO(Evans)`: confirm in the V4.2 manual). **The NUCLEO is not connected to CAN1 in
  this procedure**; the PC adapter is the only master. Node ID 1, 500 kbit/s: confirm the drive's
  own settings (`TODO(Arka)`: how they are set on this drive revision; record the firmware and serial).
- Host: `python3 -m pip install python-can`, `sudo apt-get install can-utils`. Adapter type and
  interface name `TODO(Arka)`; below it is `can1`.
- Record the git commit of this repository.

## Step 1. Termination, power off (about 60 ohm)

Follow `tools/bench/termination_check.md` for CAN1, supply off, battery disconnected, nothing
bench-powered. Pass: 55 to 65 ohm CAN_H to CAN_L, about 120 ohm with one end removed, no short to ground.
Which two nodes carry the terminations (adapter and drive, or on-board resistors) is `TODO(Evans)`.
FAIL: stop, fix the wiring, repeat. Do not power the bus with a wrong reading.

## Step 2. Interface up at 500 kbit/s

```
tools/bench/can_up.sh can1 500000 --dry-run     # prints the commands only
sudo tools/bench/can_up.sh can1 500000
ip -details link show can1
```

Expected: `state UP`, `bitrate 500000`, `restart-ms 0`, `qlen 1000`; the script exits 0. Automatic
bus-off restart is off on purpose while bringing up (a node that cannot join must stay visible).

## Step 3. Power on the drive, NMT scan

Switch the drive supply on (current limit set, hand on the switch). Then:

```
python3 tools/bench/nmt_scan.py can1 --expect 1 --expect-period-ms 100
```

Expected (emulator output, real values may differ):
```
node 0x01 heartbeats=20 nmt=0x05 period=104 ms 1000h=0x00000000
PASS
```
Pass: node 1 heard, heartbeat period within 20 percent of 100 ms (1017h is read in step 4), the
drive answers an SDO read of 1000h. Record the `1000h` value and the NMT state (0x05 is operational,
0x7F pre-operational). Exit code 1 = FAIL: check wiring, bit rate, node ID, drive power; `candump -ta can1`
shows whether anything is on the bus. A drive that sends a boot-up frame (`701#00`) and then nothing
is a bit-rate or node problem.

Also record the bus with: `candump -L can1 > $(date +%F)-can1-idle.log` for 10 s.

## Step 4. Faults and the current 2000h

```
python3 tools/bench/zlac_read_faults.py can1
```

Expected (emulator): 
```
603Fh = 0x00000000
6041h = 0x0030 (fault bit clear)
2000h = 0 ms (link-loss protection OFF)
1017h = 100 ms
PASS
```
Pass: 603Fh is 0, the status word fault bit (bit 3) is clear. FAIL (exit 1): the script prints the
decoded faults; record them, do not continue to the spin steps until the cause is named
(typical on a bench: under-voltage because of the current limit, a Hall or encoder fault from a
missing motor cable). The 2000h value read here is the drive's factory or last saved value;
record it. 2000h = 0 is expected on a new drive and is the problem step 5 fixes.

## Step 5. Set 2000h, read it back, then save

```
python3 tools/bench/zlac_set_link_loss.py can1 --timeout-ms 250
python3 tools/bench/zlac_set_link_loss.py can1 --timeout-ms 250 --save-confirmed
```

250 ms is the proposed value inside the 200 to 300 ms range of D-24 (the final value is Evans's
ruling, `TODO(Evans)`); the script refuses values outside 200 to 300.

Expected:
```
2000h before: 0 ms
2000h written 250, read back 250
not saved (no --save-confirmed): value is lost at power off
PASS
```
and from the second command `2010h = 1 written (saved to EEPROM); 2000h now 250`. Pass: the
read-back equals the written value. **Save only after a correct read-back** (the second command is
the save). The script does not save on a mismatch (exit 1) and exits 2 if the drive does not answer.

Persistence check: switch the drive supply off for 10 s, on again, and run
`python3 tools/bench/zlac_read_faults.py can1`. Pass: `2000h = 250 ms` after the power cycle. If it shows 0,
the save did not work: record, and the link-loss test below is invalid.
Also record the ramp times 6083h and 6084h as found on the drive (read with the SDO client of
`tools/bench/bench_common.py`; the object and units are in `oamr/zlac8015d.h` of #40; the unit is
`TODO(Evans)` to confirm against the V4.2 manual).

## Step 6. Bus load, idle

```
tools/bench/busload.sh can1 500000 30
```

Expected: the highest load seen is at most `MAX_LOAD_PERCENT` (script default 50, a placeholder, not a
decision: `TODO(Evans)` the agreed limit); the script exits 0. With only drive heartbeats the load is a
few percent. Run it again during step 8 (spinning) and record both peaks.

## Step 7. Two-person checkpoint before motion

Both say aloud: wheels off the ground, nothing near the wheel, stop switch in reach and tested,
2000h saved as 250 and read back after a power cycle, wheel diameter measured. Write both names in the record.
Without all of these, stop here.

## Step 8. Wheels-up spin at 0.05 m/s

```
python3 tools/bench/zlac_spin.py can1 --wheels-up-confirmed --wheel-diameter-m D --speed 0.05 --duration 10
```

`D` = the measured wheel diameter in metres (`TODO(Evans)`; as a check: 0.05 m/s on a 0.2 m wheel is 5 r/min).
Expected (emulator, D = 0.2):
```
commanded 5 r/min, measured 5.0 r/min
PASS
```
Pass: measured speed within 10 percent of the commanded value after 1 s; the wheel turns slowly
and steadily; stops at the end of the duration (the script writes zero and disables the drive).
Without `--wheels-up-confirmed` the script refuses (exit 2, nothing is sent). Try the negative sign
once (`--speed -0.05`) and check the direction is reversed. Record the commanded and measured r/min,
sound, heat after a minute, and `zlac_read_faults.py can1` afterwards. Pass: no fault.

If `commanded` is 0 r/min the diameter is too large for the speed (rounding): fix the value.
**Stop condition:** the wheel moves before the script is started, moves faster than expected, or the
supply folds back: cut the supply.

## Step 9. Link-loss test, 2000h = 250 ms, repeated 5 times (acceptance)

Follow `tools/bench/zlac_link_loss_test.md` for the full text. Short form:

1. `candump -ta can1 > run-N.log` in a second terminal (N = 1 to 5). Also record the video at 60 fps
   or more, wheel marked with tape, or a scope or tachometer on the wheel or encoder.
2. `python3 tools/bench/zlac_spin.py can1 --wheels-up-confirmed --wheel-diameter-m D --speed 0.05 --duration 60`
3. When the wheel turns steadily, **unplug the CAN1 connector** at the adapter or base controller
   side. Note the time (the video clock).
4. The wheel must stop without any new command. Ctrl-C the script (it will print a warning about
   the stop write; the bus is gone, that is expected). Plug CAN1 back. Run
   `python3 tools/bench/zlac_read_faults.py can1`. Record any fault.
5. Repeat until 5 runs pass in a row.

Pass (roadmap risk 2): the wheels stop within the set time in all 5 runs. The measured time is
recorded for each run. The measured limit in milliseconds (2000h plus the deceleration set in 6084h)
is `TODO(Evans)`: P-03 gives the 200 to 300 ms setting, not a stop-time limit. Compute the stop time as t1 minus t0
where t0 is the last host frame in `run-N.log` (the last `601` frame) and t1 is standstill on the video.

An unplug removes the last frame time from the log if the adapter cannot send into an open bus, so
also do the **silent host** variant, which has a clean t0 because the bus stays up and the drive
heartbeat (`701`) stays visible:

```
python3 tools/bench/zlac_spin.py can1 --wheels-up-confirmed --wheel-diameter-m D --speed 0.05 --duration 60 &
sleep 5; kill -9 $!
```

`kill -9` ends the script without its stop write, so the host is silent with the bus intact (the
case of a host crash). Expected with 2000h = 250: the wheel stops. Repeat 5 times too. Record both variants.

## Step 10. Control case, 2000h = 0 (the test must tell the two cases apart)

2000h = 0 is the factory value and **the wheel keeps its last speed** when the host goes silent.
This step is dangerous in that sense and is done once, at the end, with the stop switch in hand.
`zlac_set_link_loss.py` refuses 0 by design, so write it directly (not saved) with a short snippet
run from the repository root:

```
python3 - <<'EOF'
import sys
sys.path.insert(0, "tools/bench")
from bench_common import SdoClient, open_bus
bus = open_bus("can1")
sdo = SdoClient(bus, 1, timeout=0.5)
sdo.write(0x2000, 0, 0, 2)          # not saved: lost at power off
print("2000h now", sdo.read(0x2000, 0), "ms")
bus.shutdown()
EOF
```

Then run the spin script, do the `kill -9` variant of step 9, and **watch**: the expected result is
that the wheel **keeps turning** at 0.05 m/s. Then cut the supply (the stop switch) to stop it.
Record that it kept turning. Switch the drive off and on, then re-run
`zlac_read_faults.py can1`: 2000h must be back at the saved 250. If the wheel stopped with 2000h = 0
the drive has another watchdog (say which, `TODO(Evans)`: confirm with the vendor what the drive counts as communication), and the
test of step 9 does not prove 2000h; report it.

## Step 11. Fault read-out check

With the drive idle, induce one safe fault that needs no extra equipment, for example an
under-voltage by lowering the bench supply below the drive's limit (value `TODO(Evans)` from the V4.2 manual; not below the supply
minimum), or removing the Hall cable with the motor unpowered, and check that
`zlac_read_faults.py can1` names it (`603Fh` decoded: over-voltage, under-voltage, over-current,
Hall, encoder, and so on, per side). Record the raw 603Fh and the text. A fault clears with controlword
0x80 (the scripts do not do it; the firmware adapter of #40 does it on request). Do not induce an over-voltage.

## Pass criteria for the whole procedure

- Steps 1 to 5 pass; 2000h = 250 survives a power cycle.
- Step 8 passes at 0.05 m/s in both directions.
- Step 9: 5 of 5 stop runs in each variant, times recorded.
- Step 10: the control case keeps turning at 2000h = 0.

## Record

Drive serial and firmware, supply voltage and limit, wheel diameter, the outputs of steps 3 to 8,
the 10 stop times (5 unplug, 5 silent host), the control case, all `candump -L` logs, video file names.

## Open values (TODO)

- `TODO(Evans)` wheel diameter (measured), 2000h final value (proposal 250 ms), stop-time limit,
  bus load limit, ramp-time unit and values, under-voltage threshold, CAN wiring and ground for the drive.
- `TODO(Evans)` bench supply current limit and the way the drive is switched on given the 50 to 60 A switch-on current.
- `TODO(Arka)` CAN adapter and interface name, how node ID and bit rate are set on this drive revision, video setup.
