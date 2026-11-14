#!/usr/bin/env python3
"""zlac_set_link_loss.py: commission the ZLAC8015D link-loss objects, procedure of D-24.

Proves, in this order: the speed resolution 2026h:05 reads 1; 2000h (communication power-down
protection time) and 1017h (heartbeat producer time) hold the intended values after write and
read-back; the values are saved with 2010h = 1 only after an acceptance record is supplied;
and, after a power cycle of the drive, the saved values read back. Does not move a wheel, but
it writes drive configuration: it needs the owner's bench procedure and a second person at the
hardwired E-stop, and it refuses to run without --i-am-at-the-bench.

All values are provisional (D-24, pending bench traces). They are parameters, not defaults:
  --link-loss-ms  2000h, D-24 proposes 250 (accepted range 200 to 300)
  --heartbeat-ms  1017h, D-24 proposes 100; TODO(Evans): the unit of 1017h is to be settled by
                  a CAN capture, this script writes the number as given and reads it back
None of them is a stopping time.

Step 1 (default mode): read 2026h:05 (must be 1, otherwise exit 2 and nothing is written),
  read 2000h and 1017h, write both, read both back. Nothing is saved: a power cycle of the
  drive loses the values.
Step 2: run the link-loss test with zlac_link_loss_test.md, both wheels observed independently,
  and write the acceptance record (see below).
Step 3: run again with --acceptance-record FILE. The record is checked against the values being
  written; if it is missing or incomplete the script refuses before it writes anything. Only
  then 2010h = 1 is written, after the read-back matched.
Step 4: start the command with --verify-power-cycle, then power-cycle the drive. That mode
  writes nothing, waits for the drive's boot-up message (--boot-wait-s), and reads 2026h:05,
  2000h and 1017h again. Without a boot-up message it fails: the drive was not power-cycled.

Acceptance record: a JSON file written by the person who ran the test, with these keys
  accepted_by (text), date (text), link_loss_ms, heartbeat_ms (must equal the arguments),
  runs_passed (integer, at least 5 as in the roadmap acceptance), drive_behaviour
  ("ramp", "freewheel" or "fault"), wheel_sub1_observed and wheel_sub2_observed (true: each
  wheel was watched independently; the sub-indexes carry no left or right meaning),
  stop_time_ms and stop_distance_mm (objects with keys "sub1" and "sub2", numbers above 0).
The script does not judge the stop time: the acceptance limit is TODO(Evans).

Exit 0 pass. Exit 1 read-back mismatch, a missing boot-up message, or a record that does not
match. Exit 2 refused, or the drive did not answer.

Usage: zlac_set_link_loss.py can1 --i-am-at-the-bench --link-loss-ms 250 --heartbeat-ms 100
       [--acceptance-record record.json] [--node-id 1]
       zlac_set_link_loss.py can1 --i-am-at-the-bench --verify-power-cycle
       --link-loss-ms 250 --heartbeat-ms 100
"""
import argparse
import json
import sys
import time

from bench_common import (SdoAbort, SdoClient, add_bench_args, open_bus, refuse, require_bench,
                          require_speed_resolution)

MIN_MS, MAX_MS = 200, 300  # P-03 key facts: set 200 to 300 ms
MIN_RUNS = 5  # roadmap acceptance: 5 passing repeats
BEHAVIOURS = ("ramp", "freewheel", "fault")


def check_record(path, link_loss_ms, heartbeat_ms):
    """Return a list of problems with the acceptance record; empty means it is accepted."""
    try:
        with open(path, encoding="utf-8") as fh:
            rec = json.load(fh)
    except (OSError, ValueError) as exc:
        return ["cannot read acceptance record %s: %s" % (path, exc)]
    if not isinstance(rec, dict):
        return ["acceptance record is not a JSON object"]
    problems = []
    for key in ("accepted_by", "date"):
        if not isinstance(rec.get(key), str) or not rec[key].strip():
            problems.append("%s missing or empty" % key)
    for key, want in (("link_loss_ms", link_loss_ms), ("heartbeat_ms", heartbeat_ms)):
        if rec.get(key) != want:
            problems.append("%s is %r, the value being written is %d" % (key, rec.get(key), want))
    runs = rec.get("runs_passed")
    if not isinstance(runs, int) or isinstance(runs, bool) or runs < MIN_RUNS:
        problems.append("runs_passed must be an integer of at least %d" % MIN_RUNS)
    if rec.get("drive_behaviour") not in BEHAVIOURS:
        problems.append("drive_behaviour must be one of %s" % ", ".join(BEHAVIOURS))
    for key in ("wheel_sub1_observed", "wheel_sub2_observed"):
        if rec.get(key) is not True:
            problems.append("%s must be true" % key)
    for key in ("stop_time_ms", "stop_distance_mm"):
        val = rec.get(key)
        for sub in ("sub1", "sub2"):
            num = val.get(sub) if isinstance(val, dict) else None
            if (not isinstance(num, (int, float)) or isinstance(num, bool)
                    or not num > 0):
                problems.append("%s.%s must be a number above 0" % (key, sub))
    return problems


def read_state(sdo):
    return {"resolution": sdo.read(0x2026, 5), "link_loss": sdo.read(0x2000, 0),
            "heartbeat": sdo.read(0x1017, 0)}


def wait_boot_up(bus, node_id, seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        m = bus.recv(timeout=max(0.0, min(0.2, end - time.monotonic())))
        if (m is not None and not m.is_extended_id and m.arbitration_id == 0x700 + node_id
                and len(m.data) >= 1 and m.data[0] == 0x00):
            return True
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("channel")
    ap.add_argument("--link-loss-ms", dest="link_loss_ms", type=int,
                    required=True, help="2000h, provisional D-24 value 250, range 200 to 300")
    ap.add_argument("--heartbeat-ms", type=int, required=True,
                    help="1017h, provisional D-24 value 100, unit TODO(Evans)")
    ap.add_argument("--acceptance-record", metavar="FILE",
                    help="JSON acceptance record; only with it is 2010h = 1 written")
    ap.add_argument("--verify-power-cycle", action="store_true",
                    help="after a drive power cycle: write nothing, read the values back")
    ap.add_argument("--boot-wait-s", type=float, default=60.0,
                    help="how long --verify-power-cycle waits for the boot-up message")
    ap.add_argument("--node-id", type=int, default=1)
    add_bench_args(ap)
    args = ap.parse_args()
    require_bench(args)
    if not MIN_MS <= args.link_loss_ms <= MAX_MS:
        refuse("refusing: --link-loss-ms must be %d to %d" % (MIN_MS, MAX_MS))
    if not 1 <= args.heartbeat_ms <= 0xFFFF:
        refuse("refusing: --heartbeat-ms must be 1 to 65535")
    if args.verify_power_cycle and args.acceptance_record:
        refuse("refusing: --verify-power-cycle writes nothing; do not combine it with "
               "--acceptance-record")
    if args.acceptance_record:
        problems = check_record(args.acceptance_record, args.link_loss_ms, args.heartbeat_ms)
        if problems:
            for p in problems:
                print("acceptance record: " + p, file=sys.stderr)
            refuse("refusing: acceptance record not accepted, nothing was written")
    bus = open_bus(args.channel)
    try:
        sdo = SdoClient(bus, args.node_id, timeout=0.5)
        try:
            if args.verify_power_cycle:
                return verify(bus, sdo, args)
            require_speed_resolution(sdo)
            before = read_state(sdo)
            print("before: 2000h = %d, 1017h = %d" % (before["link_loss"], before["heartbeat"]))
            sdo.write(0x2000, 0, args.link_loss_ms, 2)
            sdo.write(0x1017, 0, args.heartbeat_ms, 2)
            after = read_state(sdo)
            print("2000h written %d, read back %d" % (args.link_loss_ms, after["link_loss"]))
            print("1017h written %d, read back %d" % (args.heartbeat_ms, after["heartbeat"]))
            if (after["link_loss"], after["heartbeat"]) != (args.link_loss_ms, args.heartbeat_ms):
                print("FAIL: read-back mismatch, not saving")
                return 1
            if args.acceptance_record:
                sdo.write(0x2010, 0, 1, 2)
                print("2010h = 1 written after the acceptance record was accepted")
                print("NEXT: start --verify-power-cycle, then power-cycle the drive")
            else:
                print("not saved (no --acceptance-record): the values are lost at power off")
        except (TimeoutError, SdoAbort) as exc:
            print("FAIL: %s" % exc)
            return 2
    finally:
        bus.shutdown()
    print("PASS")
    return 0


def verify(bus, sdo, args):
    if not wait_boot_up(bus, args.node_id, args.boot_wait_s):
        print("FAIL: no boot-up message from node %d within %.0f s: the drive was not power-cycled"
              % (args.node_id, args.boot_wait_s))
        return 1
    state = read_state(sdo)
    print("after power cycle: 2026h:05 = %d, 2000h = %d, 1017h = %d" % (
        state["resolution"], state["link_loss"], state["heartbeat"]))
    ok = (state["resolution"] == 1 and state["link_loss"] == args.link_loss_ms
          and state["heartbeat"] == args.heartbeat_ms)
    print("PASS" if ok else "FAIL: values after the power cycle differ from the intended ones")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
