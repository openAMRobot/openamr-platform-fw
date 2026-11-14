#!/usr/bin/env python3
"""Parse and check a console log of the telemetry image (boards/stm32/telemetry_image).

The image prints one JSON object per line on USART3: a "boot" banner after every reset and a
"st" status line every report period. This module turns a captured log into records and checks
the properties a host can rely on without knowing what the drive did:

  - sequence continuity: after every banner the sequence starts at 0 and rises by exactly 1,
    the boot id of every status line is the one of the banner before it, and a new banner
    carries a boot id that differs from the previous one;
  - uptime: strictly rising inside a boot, one report period apart (within a tolerance);
  - age fields: every "age_us" is a non-negative integer or the string "unknown";
  - unknown is never a number: a value that is not valid has raw "unknown" and, where the
    source was never seen, age "unknown" (never 0);
  - a value reported valid has a numeric age and a numeric raw.

Usage: telemetry_log.py CONSOLE_LOG [--period-us 100000] [--tolerance-us 2000]
Exit 0 when no problem is found, 1 otherwise. Used by tools/ci/renode_telemetry.py and by
tests/host/telemetry_log/test_console_log.py.
"""
import argparse
import json
import sys

UNKNOWN = "unknown"
DRIVE_OBJECTS = (("wheel", "ch1"), ("wheel", "ch2"), ("sw",), ("fault",), ("res",))
RAW_KNOWN_VALIDITY = ("valid", "unconfirmed")
MB_SENSORS = ("uart4", "uart5", "uart7", "uart8")
TFL_SENSORS = ("usart2", "usart6")


def parse(text):
    """Return (records, problems). A record is the decoded JSON object plus "_line" (1-based)."""
    records, problems = [], []
    for number, line in enumerate(text.splitlines(), start=1):
        line = line.strip()
        if not line:
            continue
        try:
            obj = json.loads(line)
        except ValueError:
            problems.append("line %d: not JSON: %.60s" % (number, line))
            continue
        if not isinstance(obj, dict) or obj.get("t") not in ("boot", "st", "err", "fatal"):
            problems.append("line %d: unexpected record type" % number)
            continue
        obj["_line"] = number
        records.append(obj)
    return records, problems


def boots(records):
    """Split records into runs: [(banner or None, [status lines])]."""
    runs = []
    for r in records:
        if r["t"] == "boot":
            runs.append((r, []))
        elif r["t"] in ("st", "err"):
            if not runs:
                runs.append((None, []))
            runs[-1][1].append(r)
    return runs


def drive_object(status, path):
    node = status["drive"]
    for key in path:
        node = node[key]
    return node


def counter(status, key):
    """Counters are printed only when they are not zero: an absent counter is 0."""
    return status["cnt"].get(key, 0)


def _is_count(v):
    return isinstance(v, int) and not isinstance(v, bool) and v >= 0


def check_sequence(records, period_us=100000, tolerance_us=2000):
    problems = []
    previous_boot_id = None
    for banner, lines in boots(records):
        if banner is None:
            problems.append("status lines before the first boot banner")
        else:
            if previous_boot_id is not None and banner["boot"] == previous_boot_id:
                problems.append("line %d: boot id %r repeated after a reset" % (banner["_line"], banner["boot"]))
            previous_boot_id = banner["boot"]
        expected_seq, last_up = 0, None
        for s in lines:
            where = "line %d" % s["_line"]
            if banner is not None and s.get("boot") != banner["boot"]:
                problems.append("%s: boot id %r differs from the banner's %r" % (where, s.get("boot"), banner["boot"]))
            if s.get("seq") != expected_seq:
                problems.append("%s: seq %r, expected %d" % (where, s.get("seq"), expected_seq))
                expected_seq = s.get("seq") if _is_count(s.get("seq")) else expected_seq
            expected_seq += 1
            if s["t"] != "st":
                problems.append("%s: error record %r" % (where, s.get("e")))
                continue
            up = s.get("up_us")
            if not _is_count(up):
                problems.append("%s: up_us %r is not a non-negative integer" % (where, up))
                continue
            if last_up is not None:
                if up <= last_up:
                    problems.append("%s: up_us %d does not rise (previous %d)" % (where, up, last_up))
                elif abs((up - last_up) - period_us) > tolerance_us:
                    problems.append("%s: up_us step %d, expected %d +/- %d" % (where, up - last_up, period_us, tolerance_us))
            last_up = up
    return problems


def _check_age(where, name, age, problems):
    if age == UNKNOWN:
        return False
    if not _is_count(age):
        problems.append("%s: %s age_us %r is neither a non-negative integer nor \"unknown\"" % (where, name, age))
        return False
    return True


def check_ages(records):
    problems = []
    for s in records:
        if s["t"] != "st":
            continue
        where = "line %d" % s["_line"]
        drive = s["drive"]
        hb = drive["hb"]
        hb_age = _check_age(where, "hb", hb["age_us"], problems)
        if hb["st"] == "alive" and not hb_age:
            problems.append("%s: heartbeat alive without an age" % where)
        if hb["st"] in ("disabled", "waiting") and hb_age:
            problems.append("%s: heartbeat %s but an age is given" % (where, hb["st"]))
        if hb["st"] == "disabled" and hb["nmt"] != UNKNOWN:
            problems.append("%s: heartbeat disabled but an NMT state is given" % where)
        for path in DRIVE_OBJECTS:
            name = ".".join(path)
            o = drive_object(s, path)
            has_age = _check_age(where, name, o["age_us"], problems)
            raw_is_unknown = o["raw"] == UNKNOWN
            if o["v"] in RAW_KNOWN_VALIDITY:
                if raw_is_unknown:
                    problems.append("%s: %s is %s but raw is unknown" % (where, name, o["v"]))
                if not has_age:
                    problems.append("%s: %s is %s without an age" % (where, name, o["v"]))
            elif not raw_is_unknown:
                problems.append("%s: %s is %s but raw is %r (must be \"unknown\")" % (where, name, o["v"], o["raw"]))
            if o["n"] == 0 and has_age:
                problems.append("%s: %s was never read but has age %r" % (where, name, o["age_us"]))
            if o["v"] in ("not_read", "unset", "timeout", "aborted", "protocol", "tx_failed") and has_age:
                problems.append("%s: %s is %s but has age %r" % (where, name, o["v"], o["age_us"]))
        for key, value in s["cnt"].items():
            if not _is_count(value) or value == 0:
                problems.append("%s: counter %s is %r (counters are printed only when not zero)" % (where, key, value))
        sens = s["sens"]
        for name in MB_SENSORS:
            m = sens[name]
            has_age = _check_age(where, name, m["age_us"], problems)
            if m["v"] != UNKNOWN:
                problems.append("%s: %s validity is %r; this image has no trigger, it must be unknown" % (where, name, m["v"]))
            if m["n"] == 0 and has_age:
                problems.append("%s: %s saw no frame but has an age" % (where, name))
        for name in TFL_SENSORS:
            t = sens[name]
            has_age = _check_age(where, name, t["age_us"], problems)
            if t["st"] in ("disabled", "never"):
                if has_age or t["v"] != UNKNOWN or t["mm"] != UNKNOWN:
                    problems.append("%s: %s is %s but reports a value, validity or age" % (where, name, t["st"]))
            if t["v"] is True:
                if not has_age or not _is_count(t["mm"]):
                    problems.append("%s: %s valid without a numeric distance and age" % (where, name))
            elif t["mm"] != UNKNOWN:
                problems.append("%s: %s not valid but mm is %r (must be \"unknown\")" % (where, name, t["mm"]))
    return problems


def check_log(text, period_us=100000, tolerance_us=2000):
    records, problems = parse(text)
    problems += check_sequence(records, period_us, tolerance_us)
    try:
        problems += check_ages(records)
    except (KeyError, TypeError) as exc:
        problems.append("status line misses a field: %r" % (exc,))
    return records, problems


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("log")
    p.add_argument("--period-us", type=int, default=100000)
    p.add_argument("--tolerance-us", type=int, default=2000)
    a = p.parse_args(argv)
    with open(a.log, encoding="utf-8", errors="replace") as fh:
        records, problems = check_log(fh.read(), a.period_us, a.tolerance_us)
    n_boot = sum(1 for r in records if r["t"] == "boot")
    n_status = sum(1 for r in records if r["t"] == "st")
    print("%d boot banner(s), %d status line(s), %d problem(s)" % (n_boot, n_status, len(problems)))
    for problem in problems:
        print("  " + problem)
    if n_status == 0:
        print("  no status line in the log")
        return 1
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
