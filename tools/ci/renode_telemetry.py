#!/usr/bin/env python3
"""Experimental: run the telemetry image in Renode against a Python drive stub and check its console.

Not a gate and not simulation evidence for the board. Renode has no STM32H723 platform; this uses
Renode's platforms/cpus/stm32h743.repl plus tests/renode/stm32h723-additions.repl, the PWR CSR1
ACTVOSRDY tag of tools/ci/renode_boot.py, a CAN hub with FDCAN1 and tools/ci/renode_drive_stub.py
on it, and a file backend on USART3 for the console. Each scenario boots the image, runs for some
virtual time while the stub behaves as told, and asserts on the JSON lines of the console.

Scenarios (all but "default_build" need the RenodeTest ELF, built with the unapproved test values
of tlm_params.h; "default_build" needs the Debug or Release ELF):
  nominal            drive answers, heartbeats arrive, one TF-Luna and one MB7060 send frames
  drive_silent       no frame from the drive from the start
  heartbeat_stops    heartbeats stop in the middle of the run, SDO answers go on
  wheel_abort        606Ch:02 answers with an SDO abort, 606Ch:01 keeps answering
  duplicate_late     every answer duplicated, then the 6041h answer arrives after its timeout
  reset_mid_run      machine reset in the middle: new boot id, sequence from zero
  default_build      image without test values: every open feature disabled, nothing sent

Usage: renode_telemetry.py --renode RENODE --elf-test RenodeTest.elf [--elf-default Debug.elf]
                           [--scenario NAME ...] [--out DIR] [--summary FILE]
Exit 0 all scenarios passed, 1 a scenario failed, 2 blocked (Renode missing or failed to run).
"""
import argparse
import collections
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import renode_boot  # noqa: E402  (unmodelled-access parser of the boot check)
import telemetry_log  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
ADDITIONS = ROOT / "tests" / "renode" / "stm32h723-additions.repl"
STUB = Path(__file__).resolve().parent / "renode_drive_stub.py"
UNKNOWN = telemetry_log.UNKNOWN

# Values of the RenodeTest build (tlm_params.h, TLM_TEST_PARAMS block) the assertions rely on.
HB_TIMEOUT_US = 300000
DRIVE_MAX_AGE_US = 300000
TFLUNA_STALE_US = 200000
PERIOD_US = 100000
# 115200 baud 8N1 carries 1152 characters per 100 ms period; the image caps a line at 1100.
LINE_BUDGET = 1000

NOT_MODELLED_BY_DESIGN = [
    "clock tree: Renode's RCC model does not keep the PLL and divider registers; the harness sets the "
    "SysTick model frequency to the SystemCoreClock the HAL derived, so tick timing is self-consistent, not verified",
    "baud rates and CAN bit timing: bytes and frames are delivered whole, with no bit time (115200 and 500 kbit/s are not checked)",
    "CAN acknowledge, arbitration, error frames, error counters and bus-off: the hub delivers every frame",
    "UART RX inversion (RXINV), UART errors and overrun",
    "RCC_RSR reset flags: read as 0, so the banner reports reset_cause unknown in every run",
    "GPIO levels and alternate functions (trigger lines, released TX pins), and the MPU region set-up",
    "console wire time: a status line occupies no time in Renode; on a board it takes about 87 us per character",
]


class Check:
    def __init__(self):
        self.failures = []

    def that(self, condition, message):
        if not condition:
            self.failures.append(message)
        return condition


def resc(elf, console, steps):
    lines = [
        'mach create "openamr"',
        "machine LoadPlatformDescription @platforms/cpus/stm32h743.repl",
        f"machine LoadPlatformDescription @{ADDITIONS}",
        'sysbus Tag <0x58024804, 0x58024807> "CSR1" 0x2000',
        "macro reset",
        '"""',
        f"    sysbus LoadELF @{elf}",
        "    cpu VectorTableOffset 0x08000000",
        '"""',
        "runMacro $reset",
        f"sysbus.usart3 CreateFileBackend @{console} true",
        'emulation CreateCANHub "can1"',
        "connector Connect sysbus.fdcan1 can1",
        f"include @{STUB}",
        "logLevel 2",
        'tlm_stub_attach "can1"',
    ]
    lines += steps
    lines += ["tlm_stub_report", "quit"]
    return "\n".join(lines) + "\n"


def run_for(seconds):
    return f'emulation RunFor "{seconds}"'


def stub_set(key, value):
    return f'tlm_stub_set "{key}" {value}'


# ---- scenarios: (which ELF, monitor steps, check function) -----------------
def status_lines(records):
    return [r for r in records if r["t"] == "st"]


def cnt(s, key):
    return telemetry_log.counter(s, key)


def obj(s, *path):
    return telemetry_log.drive_object(s, path)


def check_banner(c, records, params):
    banners = [r for r in records if r["t"] == "boot"]
    if not c.that(len(banners) >= 1, "no boot banner"):
        return
    b = banners[0]
    c.that(b.get("project") == "openamr-platform-fw telemetry_image", f"banner project {b.get('project')!r}")
    c.that(isinstance(b.get("commit"), str) and len(b["commit"]) >= 7 and b["commit"] != UNKNOWN,
           f"banner commit {b.get('commit')!r}")
    c.that(b.get("build") in ("Debug", "Release"), f"banner build {b.get('build')!r}")
    c.that(b.get("params") == params, f"banner params {b.get('params')!r}, expected {params!r}")
    # Renode does not model RCC_RSR: the decoder must say unknown, never invent a cause.
    c.that(b.get("reset_cause") == UNKNOWN and b.get("rcc_rsr") == "0x00000000",
           f"banner reset cause {b.get('reset_cause')!r} for RSR {b.get('rcc_rsr')!r}")
    c.that(b.get("boot") == 1 and b.get("boot_record") == "cold", f"first boot id {b.get('boot')!r} {b.get('boot_record')!r}")
    c.that(b.get("can1_bitrate") == 500000, "banner CAN1 bit rate")


def check_drive_valid(c, s, where):
    for path, raw in ((("wheel", "ch1"), 1234), (("wheel", "ch2"), -1234), (("res",), 1)):
        o = obj(s, *path)
        c.that(o["v"] == "valid" and o["raw"] == raw, f"{where}: {'.'.join(path)} is {o['v']} raw {o['raw']!r}, expected valid {raw}")
        c.that(isinstance(o["age_us"], int) and o["age_us"] <= DRIVE_MAX_AGE_US, f"{where}: {'.'.join(path)} age {o['age_us']!r}")
    sw = obj(s, "sw")
    c.that(sw["v"] == "valid" and sw["raw"] == "0x0237" and sw.get("state") == "OPERATION_ENABLED", f"{where}: statusword {sw}")
    fault = obj(s, "fault")
    c.that(fault["v"] == "valid" and fault["raw"] == "0x00000000", f"{where}: fault {fault}")


def scenario_nominal():
    # The heartbeat producer is restarted after 1 s, as a drive that restarts would: a second boot-up message.
    steps = ['tlm_stub_tfluna "sysbus.usart2" 10 150 500', 'tlm_stub_mb7060 "sysbus.uart4" 100 150', run_for("1.0"),
             stub_set("heartbeat_ms", 100), run_for("1.0")]

    def check(c, records, stub):
        check_banner(c, records, "test")
        lines = status_lines(records)
        if not c.that(len(lines) >= 18, f"{len(lines)} status lines in 2 s, expected at least 18"):
            return
        for s in lines[5:]:
            where = f"seq {s['seq']}"
            hb = s["drive"]["hb"]
            c.that(hb["st"] == "alive" and hb["nmt"] == "operational", f"{where}: heartbeat {hb}")
            c.that(isinstance(hb["age_us"], int) and hb["age_us"] < HB_TIMEOUT_US, f"{where}: heartbeat age {hb['age_us']!r}")
            c.that(s["drive"]["sdo"] == "on" and s["drive"]["bus_off"] is False, f"{where}: sdo/bus state")
            check_drive_valid(c, s, where)
            tf = s["sens"]["usart2"]
            c.that(tf["st"] == "ok" and tf["v"] is True and tf["mm"] == 1500, f"{where}: TF-Luna usart2 {tf}")
            c.that(isinstance(tf["age_us"], int) and tf["age_us"] < TFLUNA_STALE_US, f"{where}: TF-Luna age {tf['age_us']!r}")
            idle = s["sens"]["usart6"]
            c.that(idle["st"] == "never" and idle["v"] == UNKNOWN and idle["mm"] == UNKNOWN and idle["age_us"] == UNKNOWN,
                   f"{where}: TF-Luna usart6 without data {idle}")
            mb = s["sens"]["uart4"]
            c.that(mb["v"] == UNKNOWN and mb["n"] >= 1 and isinstance(mb["age_us"], int), f"{where}: MB7060 uart4 {mb}")
            c.that(s["sens"]["uart5"] == {"v": UNKNOWN, "age_us": UNKNOWN, "n": 0}, f"{where}: MB7060 uart5 {s['sens']['uart5']}")
            c.that(s["dis"] == ["mb7060_range"], f"{where}: disabled list {s['dis']}")
        last = lines[-1]
        c.that(sorted(last["cnt"]) == ["hb", "hb_boot", "rx", "sdo_ok", "tx"], f"non-zero counters in a nominal run: {last['cnt']}")
        c.that(cnt(last, "hb_boot") == 2 and cnt(last, "hb") >= 15, f"heartbeat counters {last['cnt']}")
        # The stub keeps counting for the rest of the last period, after the last line was printed.
        c.that(cnt(last, "sdo_ok") >= 80 and 0 <= stub["requests"] - cnt(last, "tx") <= 6,
               f"sdo_ok {cnt(last, 'sdo_ok')}, tx {cnt(last, 'tx')}, stub saw {stub['requests']}")
        c.that(max(len(json.dumps(s, separators=(",", ":"))) for s in lines) < LINE_BUDGET, f"status line longer than {LINE_BUDGET} characters")
    return "test", steps, check


def scenario_drive_silent():
    steps = [stub_set("silent", 1), run_for("1.5")]

    def check(c, records, stub):
        lines = status_lines(records)
        if not c.that(len(lines) >= 13, f"{len(lines)} status lines"):
            return
        c.that(lines[0]["drive"]["hb"]["st"] == "waiting", f"first line heartbeat {lines[0]['drive']['hb']}")
        for s in lines:
            where = f"seq {s['seq']}"
            hb = s["drive"]["hb"]
            c.that(hb["age_us"] == UNKNOWN and hb["nmt"] == UNKNOWN, f"{where}: heartbeat never seen but {hb}")
            c.that(hb["st"] == ("waiting" if s["up_us"] < HB_TIMEOUT_US else "lost") or abs(s["up_us"] - HB_TIMEOUT_US) < 30000,
                   f"{where}: heartbeat {hb['st']} at {s['up_us']} us")
            for path in telemetry_log.DRIVE_OBJECTS:
                o = obj(s, *path)
                c.that(o["v"] in ("not_read", "timeout") and o["raw"] == UNKNOWN and o["age_us"] == UNKNOWN and o["n"] == 0,
                       f"{where}: {'.'.join(path)} {o} with a silent drive")
        last = lines[-1]
        c.that(last["drive"]["hb"]["st"] == "lost", f"last heartbeat state {last['drive']['hb']['st']}")
        c.that(all(obj(last, *p)["v"] == "timeout" for p in telemetry_log.DRIVE_OBJECTS), "not every object timed out by the end")
        c.that(cnt(last, "rx") == 0 and cnt(last, "sdo_ok") == 0 and cnt(last, "sdo_to") >= 10 and cnt(last, "hb_lost") == 1,
               f"counters {last['cnt']}")
        c.that(stub["responses"] == 0 and stub["requests"] >= 10, f"stub {stub}")
    return "test", steps, check


def scenario_heartbeat_stops():
    steps = [run_for("1.0"), stub_set("heartbeat_ms", 0), run_for("1.0")]

    def check(c, records, stub):
        lines = status_lines(records)
        if not c.that(len(lines) >= 18, f"{len(lines)} status lines"):
            return
        before = [s for s in lines if 500000 <= s["up_us"] <= 1000000]
        after = [s for s in lines if s["up_us"] >= 1000000 + HB_TIMEOUT_US + PERIOD_US]
        c.that(before and all(s["drive"]["hb"]["st"] == "alive" for s in before), "heartbeat not alive before the stop")
        c.that(len(after) >= 4 and all(s["drive"]["hb"]["st"] == "lost" for s in after), "heartbeat not lost after the stop")
        ages = [s["drive"]["hb"]["age_us"] for s in after]
        c.that(all(isinstance(a, int) and a > HB_TIMEOUT_US for a in ages), f"lost heartbeat ages {ages}")
        c.that(all(b - a == PERIOD_US for a, b in zip(ages, ages[1:])), f"lost heartbeat age does not grow by one period: {ages}")
        c.that(all(s["drive"]["hb"]["nmt"] == "operational" for s in after), "last NMT state not kept as the last one seen")
        # The SDO answers go on: the wheel values stay valid on their own evidence.
        for s in after:
            check_drive_valid(c, s, f"seq {s['seq']} (heartbeat lost)")
        c.that(cnt(lines[-1], "hb_lost") == 1, f"counters {lines[-1]['cnt']}")
    return "test", steps, check


def scenario_wheel_abort():
    steps = [stub_set("abort_index", 0x606C), stub_set("abort_sub", 2), run_for("1.5")]

    def check(c, records, stub):
        lines = status_lines(records)
        if not c.that(len(lines) >= 13, f"{len(lines)} status lines"):
            return
        for s in lines[3:]:
            where = f"seq {s['seq']}"
            ch2 = obj(s, "wheel", "ch2")
            c.that(ch2["v"] == "aborted" and ch2["raw"] == UNKNOWN and ch2["age_us"] == UNKNOWN and ch2["n"] == 0
                   and ch2.get("abort") == "0x06020000", f"{where}: aborted channel {ch2}")
            ch1 = obj(s, "wheel", "ch1")
            c.that(ch1["v"] == "valid" and ch1["raw"] == 1234 and isinstance(ch1["age_us"], int), f"{where}: other channel {ch1}")
            c.that(obj(s, "sw")["v"] == "valid" and obj(s, "res")["v"] == "valid", f"{where}: other objects affected by the abort")
        last = lines[-1]
        c.that(cnt(last, "sdo_ab") >= 10 and cnt(last, "sdo_to") == 0, f"counters {last['cnt']}")
    return "test", steps, check


def scenario_duplicate_late():
    steps = [
        stub_set("duplicate", 1), run_for("1.0"),
        # 6041h now answers 65 ms after its request: 25 ms after the 40 ms timeout, and while the
        # next object (603Fh, 15 ms answer delay) is outstanding.
        stub_set("duplicate", 0), stub_set("response_us", 15000),
        stub_set("late_index", 0x6041), stub_set("late_us", 65000), run_for("1.5"),
    ]

    def check(c, records, stub):
        lines = status_lines(records)
        if not c.that(len(lines) >= 23, f"{len(lines)} status lines"):
            return
        dup = [s for s in lines if 500000 <= s["up_us"] <= 1000000]
        for s in dup:
            check_drive_valid(c, s, f"seq {s['seq']} (duplicated answers)")
        end_dup = dup[-1]
        c.that(cnt(end_dup, "sdo_un") + cnt(end_dup, "sdo_wo") >= 20, f"duplicates not counted: {end_dup['cnt']}")
        # A duplicate must never count as a second reading of the same request.
        c.that(cnt(end_dup, "sdo_ok") <= cnt(end_dup, "tx"), f"more good samples than requests: {end_dup['cnt']}")
        c.that(obj(end_dup, "wheel", "ch1")["n"] <= cnt(end_dup, "tx") // 5 + 1, "duplicate counted as a new sample")
        late = [s for s in lines if s["up_us"] >= 1000000 + 4 * PERIOD_US]
        c.that(len(late) >= 8, f"{len(late)} lines in the late phase")
        for s in late:
            where = f"seq {s['seq']} (late 6041h)"
            sw = obj(s, "sw")
            c.that(sw["v"] in ("timeout", "stale") and sw["raw"] == UNKNOWN, f"{where}: statusword {sw}")
            fault = obj(s, "fault")
            # The late 6041h answer arrives while 603Fh is outstanding: it must not become the fault code.
            c.that(fault["v"] == "valid" and fault["raw"] == "0x00000000", f"{where}: fault {fault}")
            c.that(obj(s, "wheel", "ch1")["raw"] == 1234 and obj(s, "wheel", "ch2")["raw"] == -1234, f"{where}: wheel values")
        c.that(late[-1]["drive"]["sw"]["v"] == "timeout" and late[-1]["drive"]["sw"]["age_us"] == UNKNOWN, f"last statusword {late[-1]['drive']['sw']}")
        first, last = late[0], late[-1]
        c.that(cnt(last, "sdo_to") - cnt(first, "sdo_to") >= 3, f"timeouts {cnt(first, 'sdo_to')} -> {cnt(last, 'sdo_to')}")
        c.that(cnt(last, "sdo_wo") - cnt(first, "sdo_wo") >= 3,
               f"late answers not counted as wrong object: {cnt(first, 'sdo_wo')} -> {cnt(last, 'sdo_wo')}")
        sw_n = [obj(s, "sw")["n"] for s in late]
        c.that(len(set(sw_n)) == 1, f"statusword read count changed during the late phase: {sw_n}")
    return "test", steps, check


def scenario_reset_mid_run():
    steps = [run_for("1.0"), "machine Reset", stub_set("heartbeat_ms", 100), run_for("1.0")]

    def check(c, records, stub):
        runs = telemetry_log.boots(records)
        if not c.that(len(runs) == 2, f"{len(runs)} boot banners, expected 2"):
            return
        (b1, l1), (b2, l2) = runs
        c.that(b1["boot"] == 1 and b1["boot_record"] == "cold", f"first banner {b1['boot']} {b1['boot_record']}")
        c.that(b2["boot"] == 2 and b2["boot_record"] == "retained", f"second banner {b2['boot']} {b2['boot_record']}")
        c.that(len(l1) >= 8 and len(l2) >= 8, f"{len(l1)} and {len(l2)} status lines around the reset")
        c.that(l1 and l1[-1]["seq"] >= 8, "first boot did not count up")
        c.that(l2 and l2[0]["seq"] == 0 and l2[0]["boot"] == 2, f"after the reset: seq {l2[0]['seq'] if l2 else None}")
        c.that(l2 and l2[0]["up_us"] < 2 * PERIOD_US, f"uptime after the reset {l2[0]['up_us'] if l2 else None}")
        if l2:
            # Nothing of the first boot is carried over: counters and read counts start again.
            c.that(cnt(l2[0], "sdo_ok") <= 5 and obj(l2[0], "wheel", "ch1")["n"] <= 1, f"state carried over the reset: {l2[0]['cnt']}")
            check_drive_valid(c, l2[-1], "last line after the reset")
    return "test", steps, check


def scenario_default_build():
    steps = ['tlm_stub_tfluna "sysbus.usart2" 10 150 500', run_for("1.0")]
    expected_disabled = ["hb", "sdo", "drive_age", "speed_res", "tfluna", "tfluna_stale", "mb7060_stale", "mb7060_range"]

    def check(c, records, stub):
        check_banner(c, records, "default")
        banners = [r for r in records if r["t"] == "boot"]
        if banners:
            names = [d.split(":")[0] for d in banners[0].get("disabled", [])]
            c.that(names == expected_disabled, f"banner disabled list {names}")
            c.that(all(": " in d for d in banners[0].get("disabled", [])), "banner gives no reason for a disabled feature")
        lines = status_lines(records)
        if not c.that(len(lines) >= 8, f"{len(lines)} status lines"):
            return
        for s in lines:
            where = f"seq {s['seq']}"
            c.that(s["params"] == "default" and s["dis"] == expected_disabled, f"{where}: params {s['params']} disabled {s['dis']}")
            hb = s["drive"]["hb"]
            c.that(hb == {"st": "disabled", "age_us": UNKNOWN, "nmt": UNKNOWN}, f"{where}: heartbeat {hb}")
            c.that(s["drive"]["sdo"] == "disabled", f"{where}: sdo {s['drive']['sdo']}")
            for path in telemetry_log.DRIVE_OBJECTS:
                o = obj(s, *path)
                c.that(o["v"] == "unset" and o["raw"] == UNKNOWN and o["age_us"] == UNKNOWN, f"{where}: {'.'.join(path)} {o}")
            tf = s["sens"]["usart2"]
            c.that(tf == {"st": "disabled", "v": UNKNOWN, "mm": UNKNOWN, "age_us": UNKNOWN, "n": 0}, f"{where}: TF-Luna {tf}")
        last = lines[-1]
        # No timeout value, no request: the default image sends nothing at all on CAN1.
        c.that(cnt(last, "tx") == 0 and stub["requests"] == 0, f"default image sent {cnt(last, 'tx')} frame(s), stub saw {stub['requests']}")
        c.that(cnt(last, "rx") >= 8 and cnt(last, "hb") == 0, f"heartbeats received {cnt(last, 'rx')}, consumed {cnt(last, 'hb')}")
        c.that(max(len(json.dumps(s, separators=(",", ":"))) for s in lines) < LINE_BUDGET, f"status line longer than {LINE_BUDGET} characters")
    return "default", steps, check


SCENARIOS = collections.OrderedDict([
    ("nominal", scenario_nominal),
    ("drive_silent", scenario_drive_silent),
    ("heartbeat_stops", scenario_heartbeat_stops),
    ("wheel_abort", scenario_wheel_abort),
    ("duplicate_late", scenario_duplicate_late),
    ("reset_mid_run", scenario_reset_mid_run),
    ("default_build", scenario_default_build),
])


def run_scenario(name, renode, elfs, out_dir, timeout):
    """Returns (status, messages, unhandled counter). status: PASS, FAIL or BLOCKED."""
    which, steps, check = SCENARIOS[name]()
    elf = elfs.get(which)
    if elf is None:
        return "BLOCKED", [f"no ELF given for the {which!r} build"], collections.Counter()
    console = out_dir / f"{name}.console.log"
    script = out_dir / f"{name}.resc"
    console.unlink(missing_ok=True)
    script.write_text(resc(Path(elf).resolve(), console, steps))
    try:
        r = subprocess.run([renode, "--disable-gui", "--console", "-e", f"include @{script}"], stdin=subprocess.DEVNULL,
                           capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired) as exc:
        return "BLOCKED", [f"Renode did not run: {exc}"], collections.Counter()
    output = renode_boot.ANSI.sub("", r.stdout + r.stderr)
    (out_dir / f"{name}.renode.log").write_text(output)
    _, unhandled, errors = renode_boot.parse(output)
    errors += [l for l in output.splitlines() if "Fatal error" in l or "failed" in l and "Command" in l]
    if errors:
        return "BLOCKED", ["Renode reported: " + errors[0].strip()], unhandled
    stub = None
    violations = []
    for line in output.splitlines():
        if line.startswith("TLM_STUB {"):
            stub = json.loads(line[len("TLM_STUB "):])
        elif line.startswith("TLM_STUB_VIOLATION"):
            violations.append(line)
    if stub is None or not console.exists():
        return "BLOCKED", ["no stub report or no console file from Renode"], unhandled
    c = Check()
    records, problems = telemetry_log.check_log(console.read_text(errors="replace"), PERIOD_US)
    for p in problems:
        c.that(False, "log check: " + p)
    c.that(not any(r["t"] == "fatal" for r in records), "the image printed a fatal record")
    # Read-only boundary seen from the bus: only SDO initiate-upload requests to node 1.
    c.that(stub["violations"] == 0, f"{stub['violations']} frame(s) that are not an SDO upload request: {violations[:3]}")
    try:
        check(c, records, stub)
    except (KeyError, IndexError, TypeError) as exc:
        c.that(False, f"status line misses a field: {exc!r}")
    return ("FAIL" if c.failures else "PASS"), c.failures, unhandled


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--renode", required=True)
    p.add_argument("--elf-test", help="telemetry_image.elf of the RenodeTest preset")
    p.add_argument("--elf-default", help="telemetry_image.elf of the Debug or Release preset")
    p.add_argument("--scenario", action="append", choices=list(SCENARIOS), help="default: all")
    p.add_argument("--out", default="build/renode-telemetry", help="directory for scripts, consoles and Renode logs")
    p.add_argument("--summary", help="append a Markdown summary here")
    p.add_argument("--timeout", type=int, default=600)
    a = p.parse_args(argv)
    out_dir = Path(a.out).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    elfs = {"test": a.elf_test, "default": a.elf_default}
    results, total = [], collections.Counter()
    for name in a.scenario or list(SCENARIOS):
        status, messages, unhandled = run_scenario(name, a.renode, elfs, out_dir, a.timeout)
        total.update(unhandled)
        results.append((name, status, messages))
        print(f"{status} {name}")
        for m in messages[:12]:
            print("    " + m)
        if len(messages) > 12:
            print(f"    ... and {len(messages) - 12} more")
    counts = collections.Counter(status for _, status, _ in results)
    print(f"Renode telemetry scenarios: {counts['PASS']} passed, {counts['FAIL']} failed, {counts['BLOCKED']} blocked, of {len(results)}")
    print("Peripheral accesses Renode did not model (ignored or stubbed), summed over the scenarios:")
    for item, n in total.most_common():
        print(f"  {n:5d} x {item}")
    print("Not modelled by the set-up itself:")
    for item in NOT_MODELLED_BY_DESIGN:
        print("  - " + item)
    if a.summary:
        with open(a.summary, "a", encoding="utf-8") as fh:
            fh.write("### Renode telemetry scenarios (experimental, H743 model, not a gate)\n\n| Scenario | Result |\n|---|---|\n")
            for name, status, messages in results:
                fh.write(f"| {name} | {status}{': ' + messages[0] if messages else ''} |\n")
            fh.write("\n| Not modelled | Accesses |\n|---|---|\n")
            for item, n in total.most_common():
                fh.write(f"| {item} | {n} |\n")
            fh.write("\n" + "\n".join("- " + item for item in NOT_MODELLED_BY_DESIGN) + "\n")
    if counts["BLOCKED"]:
        return 2
    return 1 if counts["FAIL"] else 0


if __name__ == "__main__":
    sys.exit(main())
