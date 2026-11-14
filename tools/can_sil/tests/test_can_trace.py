"""Trace record and replay in candump -L format (tools/can_sil/can_trace.py)."""
import os
import subprocess
import sys
import threading
import time

import pytest

import can_trace as t
import ctl017_emulator as c
from canopen_common import SdoAbort, SdoClient
from conftest import wait_for
from zlac8015d_emulator import Zlac8015dEmulator

TOOL = os.path.join(os.path.dirname(__file__), "..", "can_trace.py")


def session(master, sdo_node=1):
    """A short, deterministic host session against a ZLAC emulator."""
    sdo = SdoClient(master, sdo_node, timeout=0.5)
    sdo.read(0x2000, 0)
    sdo.write(0x2000, 0, 250, 2)
    sdo.write(0x6060, 0, 3, 1, signed=True)
    for cw in (0x06, 0x07, 0x0F):
        sdo.write(0x6040, 0, cw, 2)
    sdo.write(0x60FF, 1, 30, 4, signed=True)
    sdo.write(0x60FF, 2, 30, 4, signed=True)
    time.sleep(0.4)
    sdo.read(0x606C, 1, signed=True)
    sdo.read(0x606C, 2, signed=True)
    with pytest.raises(SdoAbort):
        sdo.read(0x7777, 0)
    with pytest.raises(SdoAbort):
        sdo.write(0x6041, 0, 1, 2)  # read only
    sdo.write(0x60FF, 1, 0, 4, signed=True)
    sdo.write(0x60FF, 2, 0, 4, signed=True)
    sdo.write(0x6040, 0, 0x06, 2)


def record_session(make_bus, tmp_path):
    zlac = Zlac8015dEmulator(make_bus(), node_id=1).start()
    sniffer, master = make_bus(), make_bus()
    frames = []
    stop = threading.Event()
    th = threading.Thread(target=lambda: frames.extend(
        t.record(sniffer, 30, "vcan0", stop.is_set)), daemon=True)
    th.start()
    time.sleep(0.1)
    try:
        session(master)
        time.sleep(0.2)
    finally:
        stop.set()
        th.join(5)
        zlac.stop()
    path = str(tmp_path / "bench.log")
    t.write_trace(path, frames, header="recorded in the test")
    return path, frames


# --- format ---------------------------------------------------------------------------

LINES = ["(1731600000.123456) can1 601#2B00200000000000",
         "(1731600000.200000) can1 701#05",
         "(1731600000.300000) can1 7FF#",
         "(1731600000.400000) can1 123#R",
         "(1731600000.500000) can1 18FEF100#0102030405060708"]


@pytest.mark.parametrize("line", LINES)
def test_line_round_trip(line):
    assert t.format_line(t.parse_line(line)) == line


def test_parse_fields():
    f = t.parse_line(LINES[0])
    assert f.arb_id == 0x601 and f.data == bytes.fromhex("2B00200000000000") and not f.extended
    assert t.parse_line(LINES[3]).remote and t.parse_line(LINES[4]).extended


@pytest.mark.parametrize("bad", [
    "", "garbage", "(1.0) can1 601#2B0", "(1.0) can1 601#" + "00" * 9, "(1.0) can1 6#00",
    "(1.0) can1 FFF#00", "(x) can1 601#00", "(1.0) can1 601 00", "(1.0) 601#00",
    "(1.0) can1 601#GG", "(1.0) can1 20000000#00"])
def test_malformed_lines_are_errors(bad):
    with pytest.raises(t.TraceError):
        t.parse_line(bad)


def test_read_trace_comments_blank_lines_and_errors(tmp_path):
    p = tmp_path / "a.log"
    p.write_text("# header\n\n" + LINES[0] + "\nnot a frame\n" + LINES[1] + "\n")
    with pytest.raises(t.TraceError) as e:
        t.read_trace(str(p))
    assert "line 4" in str(e.value)
    frames, errors = t.read_trace(str(p), lenient=True)
    assert len(frames) == 2 and len(errors) == 1


def test_read_trace_rejects_backwards_timestamps(tmp_path):
    p = tmp_path / "a.log"
    p.write_text(LINES[1] + "\n" + LINES[0].replace("123456", "000001").replace(
        "1731600000", "1731599999") + "\n")
    with pytest.raises(t.TraceError) as e:
        t.read_trace(str(p))
    assert "backwards" in str(e.value)


def test_empty_trace_and_missing_file(tmp_path):
    p = tmp_path / "e.log"
    p.write_text("")
    assert t.read_trace(str(p)) == ([], [])
    with pytest.raises(OSError):
        t.read_trace(str(tmp_path / "nope.log"))


def test_file_round_trip_keeps_every_frame(tmp_path):
    frames = [t.parse_line(x) for x in LINES]
    p = str(tmp_path / "r.log")
    t.write_trace(p, frames, header="two\nlines")
    back, errors = t.read_trace(p)
    assert errors == [] and back == frames


def test_candump_log_is_readable_by_python_can(tmp_path):
    """The format is candump -L: python-can's own reader accepts what we write."""
    can_io = pytest.importorskip("can.io")
    p = str(tmp_path / "r.log")
    t.write_trace(p, [t.parse_line(x) for x in LINES[:3] + LINES[4:]])
    with can_io.CanutilsLogReader(p) as reader:
        msgs = list(reader)
    assert [m.arbitration_id for m in msgs] == [0x601, 0x701, 0x7FF, 0x18FEF100]
    assert msgs[3].is_extended_id and bytes(msgs[0].data) == bytes.fromhex("2B00200000000000")


# --- record and replay on vcan ----------------------------------------------------------

def test_record_captures_a_session_with_both_directions(vcan, make_bus, tmp_path):
    path, frames = record_session(make_bus, tmp_path)
    ids = {f.arb_id for f in frames}
    assert {0x601, 0x581, 0x701} <= ids
    back, errors = t.read_trace(path)
    assert errors == [] and len(back) == len(frames) >= 25


def test_replay_resends_the_chosen_frames_with_spacing(vcan, make_bus):
    listener, sender = make_bus(), make_bus()
    base = time.time()
    frames = [t.Frame(base, "vcan0", 0x123, b"\x01"), t.Frame(base + 0.2, "vcan0", 0x124, b"\x02"),
              t.Frame(base + 0.4, "vcan0", 0x123, b"\x03", False, False)]
    got = []
    th = threading.Thread(target=lambda: got.extend(t.record(listener, 1.2, "vcan0")))
    th.start()
    time.sleep(0.1)
    start = time.monotonic()
    n = t.replay(sender, frames, ids={0x123})
    took = time.monotonic() - start
    th.join()
    assert n == 2 and 0.35 < took < 0.9
    assert [(f.arb_id, f.data) for f in got] == [(0x123, b"\x01"), (0x123, b"\x03")]
    assert t.replay(sender, frames, ids={0x555}) == 0


def test_replayed_session_against_a_fresh_emulator_matches(vcan, make_bus, tmp_path):
    path, _ = record_session(make_bus, tmp_path)
    frames, _ = t.read_trace(path)
    fresh = Zlac8015dEmulator(make_bus(), node_id=1).start()
    try:
        problems = t.check_trace(make_bus(), frames, 1, "zlac", exact=True, speed=0)
    finally:
        fresh.stop()
    assert problems == []


def test_regression_run_detects_a_changed_emulator(vcan, make_bus, tmp_path):
    path, _ = record_session(make_bus, tmp_path)
    frames, _ = t.read_trace(path)
    fresh = Zlac8015dEmulator(make_bus(), node_id=1)
    fresh.od.entries[(0x2000, 0)]["rw"] = "ro"  # a regression: 2000h no longer writable
    fresh.start()
    try:
        problems = t.check_trace(make_bus(), frames, 1, "zlac", exact=True, speed=0)
    finally:
        fresh.stop()
    assert problems and any("2000h" in p or "abort" in p for p in problems)


def test_regression_run_detects_a_changed_default_only_in_exact_mode(vcan, make_bus, tmp_path):
    path, _ = record_session(make_bus, tmp_path)
    frames, _ = t.read_trace(path)
    for exact, expect in ((False, False), (True, True)):
        fresh = Zlac8015dEmulator(make_bus(), node_id=1)
        fresh.od.set(0x2000, 0, 7)  # the first request of the session reads 2000h (was 0)
        fresh.start()
        try:
            problems = t.check_trace(make_bus(), frames, 1, "zlac", exact=exact, speed=0)
        finally:
            fresh.stop()
        assert bool(problems) is expect, problems


def test_regression_run_fails_when_the_node_is_silent(vcan, make_bus, tmp_path):
    path, _ = record_session(make_bus, tmp_path)
    frames, _ = t.read_trace(path)
    problems = t.check_trace(make_bus(), frames[:6], 1, "zlac", speed=0, response_timeout=0.1)
    assert problems and all("no response" in p for p in problems)


def test_a_trace_without_requests_is_a_failure_not_a_pass(vcan, make_bus):
    frames = [t.parse_line(LINES[1])]
    assert t.check_trace(make_bus(), frames, 1, "zlac", speed=0) == [
        "the trace holds no SDO request to node 1"]


def test_ctl017_profile_replays_a_recorded_sdo_session(vcan, make_bus, tmp_path):
    node = c.Ctl017Emulator(make_bus(), start_pos_mm=c.SENSOR_POS_MM[1]).start()
    sniffer, master = make_bus(), make_bus()
    frames, stop = [], threading.Event()
    th = threading.Thread(target=lambda: frames.extend(
        t.record(sniffer, 20, "vcan0", stop.is_set)), daemon=True)
    th.start()
    time.sleep(0.3)
    try:
        sdo = SdoClient(master, c.NODE_ID)
        assert sdo.read(0x1018, 2) == 0x11
        sdo.write(0x2101, 9, 500, 2)
        with pytest.raises(SdoAbort):
            sdo.write(0x2010, 2, 1, 1)
        with pytest.raises(SdoAbort):
            sdo.read(0x2000, 9)
    finally:
        stop.set()
        th.join(5)
        node.stop()
    fresh = c.Ctl017Emulator(make_bus(), start_pos_mm=c.SENSOR_POS_MM[1]).start()
    time.sleep(0.3)
    try:
        assert t.check_trace(make_bus(), frames, c.NODE_ID, "ctl017", speed=0) == []
    finally:
        fresh.stop()


def test_same_response_rules():
    ack = bytes.fromhex("6000200000000000")
    up2 = bytes.fromhex("4B00200000FA0000")
    up2b = bytes.fromhex("4B002000FA000000")
    ab = bytes.fromhex("8000200002000106")
    assert t.same_response(ack, ack, True, set()) is None
    assert t.same_response(None, None, True, set()) is None
    assert "no response" in t.same_response(ack, None, False, set())
    assert t.same_response(up2, up2b, False, set()) is None
    assert "data differs" in t.same_response(up2, up2b, True, set())
    assert t.same_response(up2, up2b, True, {0x2000}) is None  # volatile object
    assert "got abort" in t.same_response(ack, ab, False, set())
    assert "abort code" in t.same_response(ab, bytes.fromhex("8000200011000906"), False, set())


# --- safety of the tool itself ---------------------------------------------------------

@pytest.mark.parametrize("cmd", ["replay", "check"])
def test_real_interface_needs_the_bench_flag_and_prints_the_notice(tmp_path, cmd):
    p = tmp_path / "a.log"
    p.write_text(LINES[0] + "\n")
    extra = ["--node", "zlac"] if cmd == "check" else []
    r = subprocess.run([sys.executable, TOOL, cmd, str(p), "can1", *extra],
                       capture_output=True, text=True, timeout=20)
    assert r.returncode == 2
    assert "--i-am-at-the-bench" in r.stderr and "E-stop" in r.stderr
    assert "second person" in r.stderr


def test_record_never_transmits(vcan, make_bus):
    watcher, recorder = make_bus(), make_bus()
    sent = []
    recorder.send = lambda *a, **k: sent.append(a)
    t.record(recorder, 0.3, "vcan0")
    assert sent == [] and watcher.recv(timeout=0) is None


def test_cli_replay_and_check_on_vcan(vcan, make_bus, tmp_path):
    path, _ = record_session(make_bus, tmp_path)
    fresh = Zlac8015dEmulator(make_bus(), node_id=1).start()
    try:
        r = subprocess.run([sys.executable, TOOL, "check", path, vcan, "--node", "zlac",
                            "--exact", "--speed", "0"], capture_output=True, text=True,
                           timeout=60)
    finally:
        fresh.stop()
    assert r.returncode == 0 and "PASS" in r.stdout, r.stdout + r.stderr
    r = subprocess.run([sys.executable, TOOL, "replay", path, vcan, "--ids", "601",
                        "--speed", "0"], capture_output=True, text=True, timeout=30)
    assert r.returncode == 0 and "sent" in r.stdout
    bad = tmp_path / "bad.log"
    bad.write_text("nonsense\n")
    r = subprocess.run([sys.executable, TOOL, "replay", str(bad), vcan], capture_output=True,
                       text=True, timeout=30)
    assert r.returncode == 2 and "cannot read trace" in r.stderr
