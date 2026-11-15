"""Host test for tools/ci/telemetry_log.py against captured console logs of the telemetry image.

Run: python3 -m pytest tests/host/telemetry_log -q
 or: python3 -m unittest discover -s tests/host/telemetry_log -v

The logs under data/ were captured from USART3 of the image in Renode (H743 model) by
tools/ci/renode_telemetry.py; they are not from a board. The test checks sequence continuity and
the age fields of the real captures, and that the checker reports each kind of damage when a
capture is altered (so a checker that accepts everything would fail here).
"""
import copy
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "ci"))
import telemetry_log as tl  # noqa: E402

DATA = Path(__file__).resolve().parent / "data"
NOMINAL = (DATA / "renode_nominal.console.log").read_text()
RESET = (DATA / "renode_reset_mid_run.console.log").read_text()
DEFAULT = (DATA / "renode_default_build.console.log").read_text()
AUX = (DATA / "renode_aux_buses.console.log").read_text()


def dump(records):
    """Records back to console text (without the parser's own "_line" and "_len" keys)."""
    lines = []
    for r in records:
        r = {k: v for k, v in r.items() if k not in ("_line", "_len")}
        lines.append(json.dumps(r, separators=(",", ":")))
    return "\n".join(lines) + "\n"


def altered(text, change):
    """Parse, apply change(records) and return the checker's problems for the result."""
    records, problems = tl.parse(text)
    assert not problems
    records = copy.deepcopy(records)
    change(records)
    return tl.check_log(dump(records))[1]


def status(records, n):
    return [r for r in records if r["t"] == "st"][n]


def slow(records, n, part=None):
    return tl.slow_lines(records, part)[n]


class CapturedLogs(unittest.TestCase):
    def test_nominal_capture_is_clean(self):
        records, problems = tl.check_log(NOMINAL)
        self.assertEqual(problems, [])
        lines = [r for r in records if r["t"] == "st"]
        self.assertGreaterEqual(len(lines), 18)
        self.assertEqual([s["seq"] for s in lines], list(range(len(lines))))
        ups = [s["up_us"] for s in lines]
        self.assertTrue(all(abs((b - a) - 100000) <= 2000 for a, b in zip(ups, ups[1:])), ups)

    def test_nominal_ages_are_numbers_and_bounded(self):
        records, _ = tl.parse(NOMINAL)
        for s in [r for r in records if r["t"] == "st"][5:]:
            for path in tl.DRIVE_OBJECTS:
                o = tl.drive_object(s, path)
                self.assertEqual(o["v"], "valid", (s["seq"], path))
                self.assertIsInstance(o["age_us"], int)
                self.assertLessEqual(o["age_us"], 300000)   # TLM_DRIVE_MAX_AGE_US of the test build
            self.assertIsInstance(s["drive"]["hb"]["age_us"], int)
            self.assertLess(s["drive"]["hb"]["age_us"], 300000)
            self.assertLess(s["sens"]["usart2"]["age_us"], 200000)

    def test_sources_never_seen_are_unknown_not_zero(self):
        records, _ = tl.parse(NOMINAL)
        for s in [r for r in records if r["t"] == "st"]:
            idle = s["sens"]["usart6"]
            self.assertEqual((idle["v"], idle["mm"], idle["age_us"]), ("unknown", "unknown", "unknown"))
        diag = tl.slow_lines(records, "diag")
        self.assertGreaterEqual(len(diag), 1)
        for d in diag:
            self.assertEqual(d["mb"]["uart5"], ["unknown", 0])
            self.assertEqual(len(d["mb"]["uart4"]), 2)   # [age_us, frames]: no range, no trigger

    def test_slow_lines_follow_every_fifth_status_line(self):
        records, _ = tl.parse(NOMINAL)
        lines = [r for r in records if r["t"] == "st"]
        seqs = [r["seq"] for r in tl.slow_lines(records)]
        self.assertEqual(seqs, list(range(0, len(lines), tl.SLOW_EVERY)))
        self.assertEqual([r["part"] for r in tl.slow_lines(records)], [tl.slow_part(q) for q in seqs])
        self.assertTrue(all(r["_len"] <= tl.STATUS_LINE_MAX for r in lines))
        self.assertTrue(all(r["_len"] <= tl.SLOW_LINE_MAX for r in tl.slow_lines(records)))

    def test_reset_capture_restarts_the_sequence(self):
        records, problems = tl.check_log(RESET)
        self.assertEqual(problems, [])
        runs = tl.boots(records)
        self.assertEqual(len(runs), 2)
        self.assertNotEqual(runs[0][0]["boot"], runs[1][0]["boot"])
        self.assertEqual(runs[1][1][0]["seq"], 0)
        self.assertEqual({s["boot"] for s in runs[1][1]}, {runs[1][0]["boot"]})
        self.assertLess(runs[1][1][0]["up_us"], runs[0][1][-1]["up_us"])

    def test_default_build_capture_reports_everything_unknown(self):
        records, problems = tl.check_log(DEFAULT)
        self.assertEqual(problems, [])
        for s in [r for r in records if r["t"] == "st"]:
            self.assertEqual(s["drive"]["hb"], {"st": "disabled", "age_us": "unknown"})
            for path in tl.DRIVE_OBJECTS:
                o = tl.drive_object(s, path)
                self.assertEqual((o["v"], o["raw"], o["age_us"]), ("unset", "unknown", "unknown"))
            self.assertEqual(tl.counter(s, "tx"), 0)
        for c in tl.slow_lines(records, "cfg"):
            self.assertIn("sdo", c["dis"])
            self.assertEqual((c["nmt"], c["sdo"]), ("unknown", "disabled"))


    def test_aux_capture_lift_follows_and_battery_stays_locked(self):
        records, problems = tl.check_log(AUX)
        self.assertEqual(problems, [])
        ax = tl.ax_lines(records)
        lines = [r for r in records if r["t"] == "st"]
        # One ax line after every status line of a period without a slow line.
        self.assertEqual([a["seq"] for a in ax], [s["seq"] for s in lines if s["seq"] % tl.SLOW_EVERY != 0])
        self.assertTrue(all(a["bms"] == "locked" for a in ax))
        positions = [a["lift"]["pos"] for a in ax]
        self.assertIn(5000, positions)
        self.assertIn(7000, positions)
        self.assertIn("unknown", positions)
        self.assertTrue(all(a["_len"] <= tl.SLOW_LINE_MAX for a in ax))


class CheckerFindsDamage(unittest.TestCase):
    def assertProblem(self, problems, fragment):
        self.assertTrue(any(fragment in p for p in problems), "no problem containing %r in %r" % (fragment, problems))

    def test_missing_line(self):
        def change(records):
            records.remove(status(records, 7))
        self.assertProblem(altered(NOMINAL, change), "seq 8, expected 7")

    def test_repeated_line(self):
        def change(records):
            records.insert(records.index(status(records, 7)), copy.deepcopy(status(records, 7)))
        problems = altered(NOMINAL, change)
        self.assertProblem(problems, "seq 7, expected 8")
        self.assertProblem(problems, "does not rise")

    def test_sequence_not_restarted_after_reset(self):
        def change(records):
            second = tl.boots(records)[1][1]
            for s in second:
                s["seq"] += 10
        self.assertProblem(altered(RESET, change), "seq 10, expected 0")

    def test_boot_id_not_changed_after_reset(self):
        def change(records):
            banner, lines = tl.boots(records)[1]
            banner["boot"] = 1
            for s in lines:
                s["boot"] = 1
        self.assertProblem(altered(RESET, change), "repeated after a reset")

    def test_stale_boot_id_in_a_status_line(self):
        def change(records):
            tl.boots(records)[1][1][3]["boot"] = 1
        self.assertProblem(altered(RESET, change), "differs from the banner")

    def test_uptime_step(self):
        def change(records):
            status(records, 9)["up_us"] += 30000
        self.assertProblem(altered(NOMINAL, change), "up_us step")

    def test_uptime_backwards(self):
        def change(records):
            status(records, 9)["up_us"] = status(records, 8)["up_us"] - 1
        self.assertProblem(altered(NOMINAL, change), "does not rise")

    def test_zero_instead_of_unknown_age(self):
        def change(records):
            status(records, 4)["sens"]["usart6"]["age_us"] = 0
        self.assertProblem(altered(NOMINAL, change), "usart6 is never but reports")

    def test_zero_instead_of_unknown_raw(self):
        def change(records):
            status(records, 4)["drive"]["wheel"]["ch1"]["raw"] = 0
        self.assertProblem(altered(DEFAULT, change), "wheel.ch1 is unset but raw is 0")

    def test_age_for_a_value_never_read(self):
        def change(records):
            status(records, 4)["drive"]["sw"]["age_us"] = 0
        self.assertProblem(altered(DEFAULT, change), "sw was never read but has age 0")

    def test_negative_or_text_age(self):
        def change(records):
            status(records, 10)["drive"]["wheel"]["ch2"]["age_us"] = -5
            status(records, 11)["drive"]["hb"]["age_us"] = "12"
        problems = altered(NOMINAL, change)
        self.assertProblem(problems, "wheel.ch2 age_us -5")
        self.assertProblem(problems, "hb age_us '12'")

    def test_valid_without_age(self):
        def change(records):
            status(records, 10)["drive"]["wheel"]["ch1"]["age_us"] = "unknown"
        self.assertProblem(altered(NOMINAL, change), "wheel.ch1 is valid without an age")

    def test_alive_heartbeat_without_age(self):
        def change(records):
            status(records, 10)["drive"]["hb"]["age_us"] = "unknown"
        self.assertProblem(altered(NOMINAL, change), "heartbeat alive without an age")

    def test_mb7060_reported_with_a_range(self):
        def change(records):
            slow(records, 0, "diag")["mb"]["uart4"] = [37, 1, 450]
        self.assertProblem(altered(NOMINAL, change), "no trigger")

    def test_missing_slow_line(self):
        def change(records):
            records.remove(slow(records, 1))
        self.assertProblem(altered(NOMINAL, change), "no slow line after seq 5")

    def test_slow_line_in_the_wrong_place(self):
        def change(records):
            moved = slow(records, 1)
            records.remove(moved)
            records.insert(records.index(status(records, 7)) + 1, moved)
        self.assertProblem(altered(NOMINAL, change), "does not follow a status line of a slow period")

    def test_slow_parts_out_of_turn(self):
        def change(records):
            slow(records, 0)["part"] = "diag"
        self.assertProblem(altered(NOMINAL, change), "expected 'cfg'")

    def test_line_over_its_cap(self):
        def change(records):
            status(records, 3)["sens"]["usart6"]["st"] = "x" * 200
        self.assertProblem(altered(NOMINAL, change), "cap 691")

    def test_counters_left_out_are_unknown(self):
        records, _ = tl.parse(NOMINAL)
        s = copy.deepcopy(status(records, 3))
        self.assertEqual(tl.counter(s, "sdo_to"), 0)
        s["cnt_more"] = 2
        self.assertIsNone(tl.counter(s, "sdo_to"))       # absent and not known to be zero
        self.assertEqual(tl.counter(s, "rx"), s["cnt"]["rx"])

        def change(recs):
            status(recs, 3)["cnt_more"] = 0
        self.assertProblem(altered(NOMINAL, change), "cnt_more is 0")

    def test_distance_without_validity(self):
        def change(records):
            status(records, 10)["sens"]["usart6"]["mm"] = 0
        self.assertProblem(altered(NOMINAL, change), "usart6")

    def test_zero_counter_printed(self):
        def change(records):
            status(records, 10)["cnt"]["sdo_to"] = 0
        self.assertProblem(altered(NOMINAL, change), "counter sdo_to is 0")

    def test_missing_ax_line(self):
        def change(records):
            records.remove(tl.ax_lines(records)[2])
        self.assertProblem(altered(AUX, change), "no ax line after seq")

    def test_ax_line_in_a_slow_period(self):
        def change(records):
            moved = tl.ax_lines(records)[0]
            records.remove(moved)
            records.insert(records.index(status(records, 5)) + 1, moved)
        self.assertProblem(altered(AUX, change), "does not follow a status line of a period without a slow line")

    def test_battery_value_while_locked(self):
        def change(records):
            tl.ax_lines(records)[3]["bms"] = 25.6
        self.assertProblem(altered(AUX, change), "a value that is not usable is never printed")

    def test_lift_position_without_a_status_frame(self):
        def change(records):
            a = tl.ax_lines(records)[3]
            a["lift"]["age_us"] = "unknown"
        self.assertProblem(altered(AUX, change), "lift status never received")

    def test_lift_position_zero_instead_of_unknown(self):
        def change(records):
            a = tl.ax_lines(records)[3]
            a["lift"]["pos"] = -1
        self.assertProblem(altered(AUX, change), "lift position -1")

    def test_cut_line(self):
        text = NOMINAL.replace('"seq":12,', '"seq":12,' + "\n", 1)
        problems = tl.check_log(text)[1]
        self.assertProblem(problems, "not JSON")
        self.assertProblem(problems, "seq 13, expected 12")


class CommandLine(unittest.TestCase):
    def test_exit_codes(self):
        self.assertEqual(tl.main([str(DATA / "renode_nominal.console.log")]), 0)
        empty = DATA.parent / "empty.tmp.log"
        empty.write_text("")
        try:
            self.assertEqual(tl.main([str(empty)]), 1)   # zero status lines is a failure, not a pass
        finally:
            empty.unlink()


if __name__ == "__main__":
    unittest.main()
