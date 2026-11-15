#!/usr/bin/env python3
"""Parse and check a console log of the telemetry image (boards/stm32/telemetry_image).

The image prints one JSON object per line on USART3: a "boot" banner after every reset, a "st"
status line every report period and, every SLOW_EVERY periods directly after that period's
status line, a "sl" slow line with the rarely changing fields, in two parts that alternate
("cfg" at seq 0, 10, 20, ..., "diag" at seq 5, 15, ...), and in every other period, directly
after its status line, an "ax" line with the CTL-017 lift (CAN3) and the Daly BMS (CAN2). This module turns a captured log
into records and checks the properties a host can rely on without knowing what the drive did:

  - sequence continuity: after every banner the sequence starts at 0 and rises by exactly 1,
    the boot id of every status line is the one of the banner before it, and a new banner
    carries a boot id that differs from the previous one;
  - uptime: strictly rising inside a boot, one report period apart (within a tolerance);
  - age fields: every "age_us" is a non-negative integer or the string "unknown";
  - unknown is never a number: a value that is not valid has raw "unknown" and, where the
    source was never seen, age "unknown" (never 0);
  - a value reported valid has a numeric age and a numeric raw;
  - line caps: a status line fits in 60 % and a slow line in 35 % of the characters 115200 baud
    carries in one report period (691 and 403 characters with the newline);
  - a slow line follows the status line of every SLOW_EVERY-th period, with its boot id and seq;
    an "ax" line follows every other status line, with its boot id and seq, capped like a slow line;
  - lift and battery: a position, state or flag never received is "unknown"; battery values
    appear only when the decoder is usable, otherwise "bms" is "locked", "disabled" or "unknown".

Usage: telemetry_log.py CONSOLE_LOG [--period-us 100000] [--tolerance-us 2000]
Exit 0 when no problem is found, 1 otherwise. Used by tools/ci/renode_telemetry.py and by
tests/host/telemetry_log/test_console_log.py.
"""
import argparse
import json
import sys

UNKNOWN = "unknown"
# Drive objects of the status line; "res" (2026h:05) is in the slow line.
DRIVE_OBJECTS = (("wheel", "ch1"), ("wheel", "ch2"), ("sw",), ("fault",))
SLOW_OBJECTS = ("res",)
RAW_KNOWN_VALIDITY = ("valid", "unconfirmed")
MB_SENSORS = ("uart4", "uart5", "uart7", "uart8")
TFL_SENSORS = ("usart2", "usart6")
SLOW_EVERY = 5
# 115200 baud 8N1 at 100 ms: 1152 characters per period (tlm_params.h).
CHARS_PER_PERIOD = 1152
STATUS_LINE_MAX = CHARS_PER_PERIOD * 60 // 100   # 691, newline included
SLOW_LINE_MAX = CHARS_PER_PERIOD * 35 // 100     # 403, newline included


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
        if not isinstance(obj, dict) or obj.get("t") not in ("boot", "st", "sl", "ax", "err", "fatal"):
            problems.append("line %d: unexpected record type" % number)
            continue
        obj["_line"] = number
        obj["_len"] = len(line) + 1   # with the newline the image sends
        records.append(obj)
    return records, problems


def boots(records):
    """Split records into runs: [(banner or None, [status lines])]. Slow lines are not included."""
    runs = []
    for r in records:
        if r["t"] == "boot":
            runs.append((r, []))
        elif r["t"] == "st" or (r["t"] == "err" and r.get("of") not in ("sl", "ax")):
            if not runs:
                runs.append((None, []))
            runs[-1][1].append(r)
    return runs


def slow_lines(records, part=None):
    return [r for r in records if r["t"] == "sl" and (part is None or r.get("part") == part)]


def ax_lines(records):
    return [r for r in records if r["t"] == "ax"]


def slow_part(seq):
    return "cfg" if (seq // SLOW_EVERY) % 2 == 0 else "diag"


def drive_object(status, path):
    node = status["drive"]
    for key in path:
        node = node[key]
    return node


def counter(status, key):
    """Counters are printed only when they are not zero: an absent counter is 0. When the line
    says "cnt_more" (counters that did not fit under the line cap), an absent counter is unknown:
    None is returned."""
    if key in status["cnt"]:
        return status["cnt"][key]
    return None if "cnt_more" in status else 0


def _is_count(v):
    return isinstance(v, int) and not isinstance(v, bool) and v >= 0


def check_slow_placement(records):
    """Every SLOW_EVERY-th status line is followed by a slow line with its boot id and seq, every
    other status line by an "ax" line with its boot id and seq, and neither appears elsewhere."""
    problems = []
    last_st = None
    owed = None   # status line that still waits for its slow line
    owed_ax = None   # status line that still waits for its ax line
    for r in records:
        where = "line %d" % r["_line"]
        if r["t"] in ("boot", "st") or (r["t"] == "err" and r.get("of") not in ("sl", "ax")):
            if owed_ax is not None:
                problems.append("line %d: no ax line after seq %r" % (owed_ax["_line"], owed_ax.get("seq")))
            owed_ax = None
        if r["t"] == "ax" or (r["t"] == "err" and r.get("of") == "ax"):
            if last_st is None or owed_ax is not last_st or r.get("seq") != last_st.get("seq") or r.get("boot") != last_st.get("boot"):
                problems.append("%s: ax line seq %r boot %r does not follow a status line of a period without a slow line"
                                % (where, r.get("seq"), r.get("boot")))
            owed_ax = None
            continue
        if r["t"] == "boot":
            if owed is not None:
                problems.append("line %d: no slow line after seq %r" % (owed["_line"], owed.get("seq")))
            last_st, owed = None, None
        elif r["t"] == "st" or (r["t"] == "err" and r.get("of") != "sl"):
            if owed is not None:
                problems.append("line %d: no slow line after seq %r" % (owed["_line"], owed.get("seq")))
            last_st = r
            owed = r if _is_count(r.get("seq")) and r["seq"] % SLOW_EVERY == 0 else None
            owed_ax = r if _is_count(r.get("seq")) and r["seq"] % SLOW_EVERY != 0 else None
        elif r["t"] == "sl" or (r["t"] == "err" and r.get("of") == "sl"):
            if last_st is None or owed is not last_st or r.get("seq") != last_st.get("seq") or r.get("boot") != last_st.get("boot"):
                problems.append("%s: slow line seq %r boot %r does not follow a status line of a slow period"
                                % (where, r.get("seq"), r.get("boot")))
            elif r["t"] == "sl" and r.get("part") != slow_part(r["seq"]):
                problems.append("%s: slow line part %r at seq %r, expected %r" % (where, r.get("part"), r["seq"], slow_part(r["seq"])))
            owed = None
    return problems


def check_lengths(records):
    problems = []
    for r in records:
        cap = {"st": STATUS_LINE_MAX, "sl": SLOW_LINE_MAX, "ax": SLOW_LINE_MAX}.get(r["t"])
        if cap is not None and r["_len"] > cap:
            problems.append("line %d: %s line of %d characters, cap %d" % (r["_line"], r["t"], r["_len"], cap))
    return problems


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
        for path in DRIVE_OBJECTS:
            _check_object(where, ".".join(path), drive_object(s, path), problems)
        if "cnt_more" in s and (not _is_count(s["cnt_more"]) or s["cnt_more"] == 0):
            problems.append("%s: cnt_more is %r (printed only when counters were left out)" % (where, s["cnt_more"]))
        for key, value in s["cnt"].items():
            if not _is_count(value) or value == 0:
                problems.append("%s: counter %s is %r (counters are printed only when not zero)" % (where, key, value))
        sens = s["sens"]
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
            if "rx" in t and t["rx"] != "failed":
                problems.append("%s: %s rx is %r; only \"failed\" is printed" % (where, name, t["rx"]))
    for s in slow_lines(records, "cfg"):
        where = "line %d" % s["_line"]
        for name in SLOW_OBJECTS:
            _check_object(where, name, s[name], problems)
    for s in slow_lines(records, "diag"):
        where = "line %d" % s["_line"]
        for name in MB_SENSORS:
            m = s["mb"][name]
            # [age_us, frames]: no range, because this image sends no trigger.
            if not isinstance(m, list) or len(m) != 2:
                problems.append("%s: %s is %r; it must be [age_us, frames] (no trigger, no range)" % (where, name, m))
                continue
            has_age = _check_age(where, name, m[0], problems)
            if not _is_count(m[1]):
                problems.append("%s: %s frame count %r" % (where, name, m[1]))
            elif m[1] == 0 and has_age:
                problems.append("%s: %s saw no frame but has an age" % (where, name))
        for name, words in s["stk"].items():
            if not _is_count(words):
                problems.append("%s: stack mark of %s is %r" % (where, name, words))
        for name, state in s["rx"].items():
            if state not in ("stopped", "failed"):
                problems.append("%s: port %s listed with state %r" % (where, name, state))
    for a in ax_lines(records):
        where = "line %d" % a["_line"]
        lift = a["lift"]
        hb_age = _check_age(where, "lift hb", lift["hb_age_us"], problems)
        if lift["hb"] == "alive" and not hb_age:
            problems.append("%s: lift heartbeat alive without an age" % where)
        if lift["hb"] in ("disabled", "waiting") and hb_age:
            problems.append("%s: lift heartbeat %s but an age is given" % (where, lift["hb"]))
        seen = _check_age(where, "lift", lift["age_us"], problems)
        if lift["pos"] != UNKNOWN and not _is_count(lift["pos"]):
            problems.append("%s: lift position %r is neither a count nor \"unknown\"" % (where, lift["pos"]))
        if not seen and (lift["pos"] != UNKNOWN or lift["st"] != UNKNOWN or lift["fl"] != UNKNOWN):
            problems.append("%s: lift status never received but reports %r %r %r" % (where, lift["st"], lift["pos"], lift["fl"]))
        if (lift["vel"] == UNKNOWN) != (lift["ma"] == UNKNOWN) or (lift["vel"] == UNKNOWN) != (lift["flt"] == UNKNOWN):
            problems.append("%s: lift TPDO2 values partly unknown: %r %r %r" % (where, lift["vel"], lift["ma"], lift["flt"]))
        bms = a["bms"]
        if isinstance(bms, dict):
            if sorted(bms) != ["age_us", "da", "mv", "soc_pm"] or not _check_age(where, "bms", bms["age_us"], problems):
                problems.append("%s: battery values %r" % (where, bms))
        elif bms not in ("locked", "disabled", UNKNOWN):
            problems.append("%s: bms is %r; a value that is not usable is never printed" % (where, bms))
    return problems


def _check_object(where, name, o, problems):
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


def check_log(text, period_us=100000, tolerance_us=2000):
    records, problems = parse(text)
    problems += check_sequence(records, period_us, tolerance_us)
    problems += check_slow_placement(records)
    problems += check_lengths(records)
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
    n_slow = sum(1 for r in records if r["t"] == "sl")
    n_ax = sum(1 for r in records if r["t"] == "ax")
    print("%d boot banner(s), %d status line(s), %d slow line(s), %d ax line(s), %d problem(s)"
          % (n_boot, n_status, n_slow, n_ax, len(problems)))
    for problem in problems:
        print("  " + problem)
    if n_status == 0:
        print("  no status line in the log")
        return 1
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
