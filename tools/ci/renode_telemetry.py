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
  sustained_traffic  bursts of 12 accepted frames every 5 ms for 2 s: the interrupt takes at most
                     4 frames per entry, the tick keeps its period, deadline misses are zero or
                     reported, nothing is lost silently
  uart_overflow_tfluna  300 bytes at once on the TF-Luna port usart2: the ring overflow is counted,
                     the buffered bytes are discarded and the next sample is valid
  reset_keep_ram     two machine resets (RAM kept): boot id 1, 2, 3, sequence from zero each time
  fresh_start        a fresh Renode start after a run that reached boot id 3: boot id 1, cold
                     record, sequence from zero
  idle_sleep         the idle task executes WFI: the CPU executes far fewer instructions than its
                     MIPS setting allows in the virtual time that passed

Every machine reset is RESET_STEPS: pause, "machine Reset", install the SystemCoreClock hook
again (a reset drops it, and the SysTick model would fall back to its default frequency), then
the next RunFor starts the machine. reset_mid_run and reset_keep_ram check that the status line
rate after a reset equals the rate before it.

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
SLOW_EVERY = telemetry_log.SLOW_EVERY
TICK_MS = 20

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
    lines += ["tlm_stub_cpu_report", "tlm_stub_report", "quit"]
    return "\n".join(lines) + "\n"


# A machine reset inside one Renode process. "machine Reset" drops the SystemCoreClock bus hook
# of the stub; without it the SysTick model runs at its default frequency after the reset and the
# image's tick (and so its status line rate) no longer matches virtual time. The machine is
# paused, reset, the hook installed again, and the next RunFor starts it.
RESET_STEPS = ["pause", "machine Reset", "tlm_stub_clock"]

# Status line rate: one line per report period. Allowed difference between the rate of two
# boots, and between a boot's rate and the nominal one, in lines per second of virtual time
# (a boot loses up to one period at its start and at its end).
LINES_PER_S = 1000000 // 100000
RATE_TOLERANCE = 2.5


def check_line_rates(c, runs, seconds, where):
    """Each boot ran for seconds[i] of virtual time: its status line rate is the nominal one and
    equals the rate of the boot before it."""
    rates = [len(lines) / d for (_, lines), d in zip(runs, seconds)]
    for i, r in enumerate(rates):
        c.that(abs(r - LINES_PER_S) <= RATE_TOLERANCE,
               f"{where}: boot {i + 1} printed {r:.1f} status lines per virtual second, expected {LINES_PER_S}")
    for i in range(1, len(rates)):
        c.that(abs(rates[i] - rates[i - 1]) <= RATE_TOLERANCE,
               f"{where}: line rate after reset {i} is {rates[i]:.1f}/s, before it {rates[i - 1]:.1f}/s")
    return rates


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


def slow_lines(records, part=None):
    return telemetry_log.slow_lines(records, part)


def cfg_lines(records):
    return slow_lines(records, "cfg")


def diag_lines(records):
    return slow_lines(records, "diag")


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


def check_res_valid(c, sl, where):
    o = sl["res"]
    c.that(o["v"] == "valid" and o["raw"] == 1 and isinstance(o["age_us"], int) and o["age_us"] <= DRIVE_MAX_AGE_US,
           f"{where}: res {o}")


def check_drive_valid(c, s, where):
    for path, raw in ((("wheel", "ch1"), 1234), (("wheel", "ch2"), -1234)):
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
            c.that(hb["st"] == "alive", f"{where}: heartbeat {hb}")
            c.that(isinstance(hb["age_us"], int) and hb["age_us"] < HB_TIMEOUT_US, f"{where}: heartbeat age {hb['age_us']!r}")
            check_drive_valid(c, s, where)
            tf = s["sens"]["usart2"]
            c.that(tf["st"] == "ok" and tf["v"] is True and tf["mm"] == 1500, f"{where}: TF-Luna usart2 {tf}")
            c.that(isinstance(tf["age_us"], int) and tf["age_us"] < TFLUNA_STALE_US, f"{where}: TF-Luna age {tf['age_us']!r}")
            idle = s["sens"]["usart6"]
            c.that(idle["st"] == "never" and idle["v"] == UNKNOWN and idle["mm"] == UNKNOWN and idle["age_us"] == UNKNOWN,
                   f"{where}: TF-Luna usart6 without data {idle}")
            c.that(sorted(s["sens"]) == ["usart2", "usart6"], f"{where}: sensors of the status line {sorted(s['sens'])}")
        slow = slow_lines(records)
        c.that([x["seq"] for x in slow] == list(range(0, len(lines), SLOW_EVERY)), f"slow lines at seq {[x['seq'] for x in slow]}")
        for sl in cfg_lines(records)[1:]:
            where = f"slow cfg seq {sl['seq']}"
            c.that(sl["params"] == "test" and sl["sdo"] == "on" and sl["nmt"] == "operational", f"{where}: {sl['params']} {sl['sdo']} {sl['nmt']}")
            c.that(sl["bus_off"] is False, f"{where}: bus state {sl['bus_off']}")
            check_res_valid(c, sl, where)
            c.that(sl["dis"] == ["mb7060_range"], f"{where}: disabled {sl['dis']}")
            c.that(sl["health"] == "available", f"{where}: SDO health {sl['health']}")
        for sl in diag_lines(records):
            where = f"slow diag seq {sl['seq']}"
            mb = sl["mb"]["uart4"]
            c.that(isinstance(mb[0], int) and mb[1] >= 1, f"{where}: MB7060 uart4 {mb}")
            c.that(sl["mb"]["uart5"] == [UNKNOWN, 0], f"{where}: MB7060 uart5 {sl['mb']['uart5']}")
            c.that(sl["rx"] == {}, f"{where}: rx {sl['rx']}")
            c.that(sorted(sl["stk"]) == ["can1_rx", "can1_tick", "idle", "reporter", "sensors"]
                   and all(w > 0 for w in sl["stk"].values()), f"{where}: stack marks {sl['stk']}")
        last = lines[-1]
        c.that(sorted(last["cnt"]) == ["hb", "hb_boot", "rx", "sdo_ok", "tx"], f"non-zero counters in a nominal run: {last['cnt']}")
        c.that(cnt(last, "hb_boot") == 2 and cnt(last, "hb") >= 15, f"heartbeat counters {last['cnt']}")
        # The stub keeps counting for the rest of the last period, after the last line was printed.
        c.that(cnt(last, "sdo_ok") >= 80 and 0 <= stub["requests"] - cnt(last, "tx") <= 6,
               f"sdo_ok {cnt(last, 'sdo_ok')}, tx {cnt(last, 'tx')}, stub saw {stub['requests']}")
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
            c.that(hb["age_us"] == UNKNOWN, f"{where}: heartbeat never seen but {hb}")
            c.that(hb["st"] == ("waiting" if s["up_us"] < HB_TIMEOUT_US else "lost") or abs(s["up_us"] - HB_TIMEOUT_US) < 30000,
                   f"{where}: heartbeat {hb['st']} at {s['up_us']} us")
            for path in telemetry_log.DRIVE_OBJECTS:
                o = obj(s, *path)
                c.that(o["v"] in ("not_read", "timeout") and o["raw"] == UNKNOWN and o["age_us"] == UNKNOWN and o["n"] == 0,
                       f"{where}: {'.'.join(path)} {o} with a silent drive")
        for sl in cfg_lines(records):
            c.that(sl["nmt"] == UNKNOWN and sl["res"]["raw"] == UNKNOWN and sl["res"]["age_us"] == UNKNOWN,
                   f"slow seq {sl['seq']}: heartbeat or resolution never seen but {sl['nmt']} {sl['res']}")
        c.that(cfg_lines(records)[-1]["health"] == "unavailable", f"SDO health with a silent drive {cfg_lines(records)[-1]['health']}")
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
        slow_after = [x for x in cfg_lines(records) if (x["seq"] + 1) * PERIOD_US >= 1000000]
        c.that(slow_after and all(x["nmt"] == "operational" for x in slow_after), "last NMT state not kept as the last one seen")
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
            c.that(obj(s, "sw")["v"] == "valid" and obj(s, "fault")["v"] == "valid", f"{where}: other objects affected by the abort")
        for sl in cfg_lines(records)[1:]:
            check_res_valid(c, sl, f"slow seq {sl['seq']}")
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
    steps = [run_for("1.0"), *RESET_STEPS, stub_set("heartbeat_ms", 100), run_for("1.0")]

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
        check_line_rates(c, runs, [1.0, 1.0], "reset mid run")
        c.that(stub["clock_hooks"] == 2, f"SystemCoreClock hook installed {stub['clock_hooks']} times, expected 2")
        if l2:
            # Nothing of the first boot is carried over: counters and read counts start again.
            c.that(cnt(l2[0], "sdo_ok") <= 5 and obj(l2[0], "wheel", "ch1")["n"] <= 1, f"state carried over the reset: {l2[0]['cnt']}")
            check_drive_valid(c, l2[-1], "last line after the reset")
    return "test", steps, check


def scenario_default_build():
    steps = ['tlm_stub_tfluna "sysbus.usart2" 10 150 500', run_for("1.0")]
    expected_disabled = ["hb", "sdo", "sdo_health_n", "sdo_health_t", "drive_age", "speed_res", "tfluna", "tfluna_stale",
                         "mb7060_stale", "mb7060_range"]

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
            hb = s["drive"]["hb"]
            c.that(hb == {"st": "disabled", "age_us": UNKNOWN}, f"{where}: heartbeat {hb}")
            for path in telemetry_log.DRIVE_OBJECTS:
                o = obj(s, *path)
                c.that(o["v"] == "unset" and o["raw"] == UNKNOWN and o["age_us"] == UNKNOWN, f"{where}: {'.'.join(path)} {o}")
            tf = s["sens"]["usart2"]
            c.that(tf == {"st": "disabled", "v": UNKNOWN, "mm": UNKNOWN, "age_us": UNKNOWN, "n": 0}, f"{where}: TF-Luna {tf}")
        slow = cfg_lines(records)
        c.that(len(slow) >= 1 and len(diag_lines(records)) >= 1, "no slow line of each part")
        for sl in slow:
            where = f"slow seq {sl['seq']}"
            c.that(sl["params"] == "default" and sl["dis"] == expected_disabled, f"{where}: params {sl['params']} disabled {sl['dis']}")
            c.that(sl["sdo"] == "disabled" and sl["health"] == "disabled" and sl["nmt"] == UNKNOWN, f"{where}: {sl['sdo']} {sl['health']} {sl['nmt']}")
            c.that(sl["res"]["v"] == "unset" and sl["res"]["raw"] == UNKNOWN, f"{where}: res {sl['res']}")
        last = lines[-1]
        # No timeout value, no request: the default image sends nothing at all on CAN1.
        c.that(cnt(last, "tx") == 0 and stub["requests"] == 0, f"default image sent {cnt(last, 'tx')} frame(s), stub saw {stub['requests']}")
        c.that(cnt(last, "rx") >= 8 and cnt(last, "hb") == 0, f"heartbeats received {cnt(last, 'rx')}, consumed {cnt(last, 'hb')}")
    return "default", steps, check


def scenario_sustained_traffic():
    # 12 heartbeat frames (accepted by the filter) at one instant every 5 ms: 2400 frames/s on top
    # of the normal traffic, each burst more than the 4 frames one interrupt entry may take.
    steps = [run_for("0.5"), stub_set("burst_n", 12), stub_set("burst_ms", 5), run_for("2.0"),
             stub_set("burst_n", 0), run_for("0.3")]

    def check(c, records, stub):
        lines = status_lines(records)
        if not c.that(len(lines) >= 26, f"{len(lines)} status lines"):
            return
        last = lines[-1]
        c.that(cnt(last, "rx") >= 4000, f"frames received {cnt(last, 'rx')}")
        # Every frame the stub sent before the last line was either received or counted lost.
        lost = cnt(last, "rx_hw_lost") + cnt(last, "rx_q_ovf")
        c.that(0 <= stub["frames_sent"] - (cnt(last, "rx") + lost) <= 40,
               f"stub sent {stub['frames_sent']}, image received {cnt(last, 'rx')} and counted {lost} lost")
        c.that(cnt(last, "rx_defer") >= 100, f"interrupt entries that left frames for the next one: {cnt(last, 'rx_defer')}")
        # The tick keeps its period: 50 runs per second; a missed deadline is counted, never made up.
        slow = diag_lines(records)   # one per second
        for a, b in zip(slow, slow[1:]):
            runs = b["tick"][0] - a["tick"][0]
            missed = cnt(next(s for s in lines if s["seq"] == b["seq"]), "tick_miss") - \
                cnt(next(s for s in lines if s["seq"] == a["seq"]), "tick_miss")
            c.that(runs <= 1000 // TICK_MS + 1, f"slow seq {b['seq']}: {runs} tick runs in one second (catch-up burst)")
            c.that(runs + missed >= 1000 // TICK_MS - 1, f"slow seq {b['seq']}: {runs} runs and {missed} reported misses in one second")
        c.that(cnt(last, "tick_miss") == 0, f"tick deadline misses under sustained traffic: {cnt(last, 'tick_miss')}")
        # The drive values stay valid on their own samples through the traffic.
        for s in lines[5:]:
            check_drive_valid(c, s, f"seq {s['seq']} (sustained traffic)")
        c.that(cnt(last, "overrun") == 0 and cnt(last, "dropped") == 0, f"reporter counters {last['cnt']}")
    return "test", steps, check


def scenario_uart_overflow_tfluna():
    # TF-Luna on usart2 every 10 ms at 150 cm; at 1.0 s, 300 bytes (33 frames at 222 cm) arrive at one
    # instant, more than the 64-byte ring.
    steps = ['tlm_stub_tfluna "sysbus.usart2" 10 150 500', run_for("1.0"),
             'tlm_stub_uart_burst "sysbus.usart2" 33 222 500', run_for("1.0")]

    def check(c, records, stub):
        lines = status_lines(records)
        if not c.that(len(lines) >= 18, f"{len(lines)} status lines"):
            return
        before = [s for s in lines if s["up_us"] < 1000000]
        after = [s for s in lines if s["up_us"] > 1000000]
        c.that(before and all(cnt(s, "ring_ovf") == 0 for s in before), "overflow counted before the burst")
        c.that(after and cnt(after[0], "ring_ovf") >= 1, f"first line after the burst: {after[0]['cnt'] if after else None}")
        c.that(cnt(lines[-1], "ring_ovf") == cnt(after[0], "ring_ovf"), "further overflows after the burst")
        # At most the ring's worth of the burst is parsed; the rest is counted lost, not parsed.
        c.that(cnt(lines[-1], "ring_lost") >= 300 - 64, f"bytes lost {cnt(lines[-1], 'ring_lost')} of a 300-byte burst")
        c.that(cnt(lines[-1], "tf_bad") == 0, f"checksum errors after the resynchronisation: {cnt(lines[-1], 'tf_bad')}")
        for s in after:
            tf = s["sens"]["usart2"]
            c.that(tf["st"] == "ok" and tf["v"] is True and tf["mm"] in (1500, 2220) and isinstance(tf["age_us"], int)
                   and tf["age_us"] < TFLUNA_STALE_US, f"seq {s['seq']} after the overflow: {tf}")
        c.that(after[-1]["sens"]["usart2"]["mm"] == 1500, "last sample is not the periodic one")
        ns = [s["sens"]["usart2"]["n"] for s in after]
        c.that(all(b > a for a, b in zip(ns, ns[1:])), f"frame count does not rise after the overflow: {ns}")
        c.that(diag_lines(records)[-1]["rx"] == {}, f"port state after the overflow: {diag_lines(records)[-1]['rx']}")
    return "test", steps, check


def check_runs_from_zero(c, runs, where):
    for banner, lines in runs:
        c.that(lines and lines[0]["seq"] == 0 and lines[0]["boot"] == banner["boot"],
               f"{where}: boot {banner['boot']} starts at seq {lines[0]['seq'] if lines else None}")
        c.that(lines and lines[0]["up_us"] < 2 * PERIOD_US, f"{where}: boot {banner['boot']} uptime {lines[0]['up_us'] if lines else None}")


def scenario_reset_keep_ram():
    steps = [run_for("1.0"), *RESET_STEPS, run_for("1.0"), *RESET_STEPS, stub_set("heartbeat_ms", 100), run_for("1.0")]

    def check(c, records, stub):
        runs = telemetry_log.boots(records)
        if not c.that(len(runs) == 3, f"{len(runs)} boot banners, expected 3"):
            return
        ids = [b["boot"] for b, _ in runs]
        c.that(ids == [1, 2, 3], f"boot ids {ids}, expected 1, 2, 3 while RAM is kept")
        c.that([b["boot_record"] for b, _ in runs] == ["cold", "retained", "retained"], "boot record states")
        check_runs_from_zero(c, runs, "reset keeping RAM")
        check_line_rates(c, runs, [1.0, 1.0, 1.0], "reset keeping RAM")
    return "test", steps, check


def scenario_fresh_start():
    # First Renode process: two resets, so the record reaches boot id 3. Second, fresh process
    # (new emulation, RAM not kept): boot id 1 again, cold record.
    before = [run_for("0.4"), *RESET_STEPS, run_for("0.4"), *RESET_STEPS, run_for("0.4")]
    steps = [run_for("0.6")]

    def check(c, records, stub, before_records):
        prior = telemetry_log.boots(before_records)
        c.that([b["boot"] for b, _ in prior] == [1, 2, 3], f"first process boot ids {[b['boot'] for b, _ in prior]}")
        runs = telemetry_log.boots(records)
        if not c.that(len(runs) == 1, f"{len(runs)} boot banners in the fresh process"):
            return
        banner, _ = runs[0]
        c.that(banner["boot"] == 1 and banner["boot_record"] == "cold", f"fresh start: boot {banner['boot']} {banner['boot_record']}")
        check_runs_from_zero(c, runs, "fresh start")
    return "test", steps, check, before


def scenario_idle_sleep():
    steps = [run_for("2.0")]

    def check(c, records, stub):
        cpu = stub.get("cpu")
        if not c.that(cpu is not None, "no TLM_CPU report from Renode"):
            return
        budget = cpu["virtual_s"] * cpu["mips"] * 1e6
        c.that(cpu["virtual_s"] >= 1.9, f"virtual time {cpu['virtual_s']} s")
        # A spinning idle loop executes about the whole budget; with WFI the CPU sleeps between
        # interrupts. Bound: a quarter of the budget (measured value in the scenario output).
        c.that(cpu["instructions"] < IDLE_BUSY_FRACTION_MAX * budget,
               f"{cpu['instructions']} instructions in {cpu['virtual_s']:.2f} s at {cpu['mips']} MIPS "
               f"({cpu['instructions'] / budget:.2%} of the budget): the idle task does not sleep")
        c.that(len(status_lines(records)) >= 18, f"{len(status_lines(records))} status lines in 2 s")
    return "test", steps, check


IDLE_BUSY_FRACTION_MAX = 0.25

SCENARIOS = collections.OrderedDict([
    ("nominal", scenario_nominal),
    ("drive_silent", scenario_drive_silent),
    ("heartbeat_stops", scenario_heartbeat_stops),
    ("wheel_abort", scenario_wheel_abort),
    ("duplicate_late", scenario_duplicate_late),
    ("reset_mid_run", scenario_reset_mid_run),
    ("default_build", scenario_default_build),
    ("sustained_traffic", scenario_sustained_traffic),
    ("uart_overflow_tfluna", scenario_uart_overflow_tfluna),
    ("reset_keep_ram", scenario_reset_keep_ram),
    ("fresh_start", scenario_fresh_start),
    ("idle_sleep", scenario_idle_sleep),
])


def run_renode(renode, elf, out_dir, tag, steps, timeout):
    """One fresh Renode process. Returns (error or None, console text, stub report, violations, unhandled)."""
    console = out_dir / f"{tag}.console.log"
    script = out_dir / f"{tag}.resc"
    console.unlink(missing_ok=True)
    script.write_text(resc(Path(elf).resolve(), console, steps))
    try:
        r = subprocess.run([renode, "--disable-gui", "--console", "-e", f"include @{script}"], stdin=subprocess.DEVNULL,
                           capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired) as exc:
        return f"Renode did not run: {exc}", "", None, [], collections.Counter()
    output = renode_boot.ANSI.sub("", r.stdout + r.stderr)
    (out_dir / f"{tag}.renode.log").write_text(output)
    _, unhandled, errors = renode_boot.parse(output)
    errors += [l for l in output.splitlines() if "Fatal error" in l or "failed" in l and "Command" in l]
    if errors:
        return "Renode reported: " + errors[0].strip(), "", None, [], unhandled
    stub = None
    cpu = None
    violations = []
    for line in output.splitlines():
        if line.startswith("TLM_STUB {"):
            stub = json.loads(line[len("TLM_STUB "):])
        elif line.startswith("TLM_CPU {"):
            cpu = json.loads(line[len("TLM_CPU "):])
        elif line.startswith("TLM_STUB_VIOLATION"):
            violations.append(line)
    if stub is None or not console.exists():
        return "no stub report or no console file from Renode", "", None, [], unhandled
    stub["cpu"] = cpu
    return None, console.read_text(errors="replace"), stub, violations, unhandled


def run_scenario(name, renode, elfs, out_dir, timeout):
    """Returns (status, messages, unhandled counter). status: PASS, FAIL or BLOCKED."""
    spec = SCENARIOS[name]()
    which, steps, check = spec[:3]
    before = spec[3] if len(spec) > 3 else None
    elf = elfs.get(which)
    if elf is None:
        return "BLOCKED", [f"no ELF given for the {which!r} build"], collections.Counter()
    c = Check()
    total = collections.Counter()
    before_records = None
    if before is not None:
        error, text, stub, violations, unhandled = run_renode(renode, elf, out_dir, name + ".before", before, timeout)
        total.update(unhandled)
        if error:
            return "BLOCKED", [error], total
        before_records, problems = telemetry_log.check_log(text, PERIOD_US)
        for p in problems:
            c.that(False, "log check (first process): " + p)
        c.that(stub["violations"] == 0, f"first process: {stub['violations']} frame(s) that are not an SDO upload request")
    error, text, stub, violations, unhandled = run_renode(renode, elf, out_dir, name, steps, timeout)
    total.update(unhandled)
    if error:
        return "BLOCKED", [error], total
    records, problems = telemetry_log.check_log(text, PERIOD_US)
    for p in problems:
        c.that(False, "log check: " + p)
    c.that(not any(r["t"] == "fatal" for r in records), "the image printed a fatal record")
    c.that(not any(r["t"] == "err" for r in records), "the image printed an error record (line over its cap)")
    # Read-only boundary seen from the bus: only SDO initiate-upload requests to node 1.
    c.that(stub["violations"] == 0, f"{stub['violations']} frame(s) that are not an SDO upload request: {violations[:3]}")
    try:
        if before is not None:
            check(c, records, stub, before_records)
        else:
            check(c, records, stub)
    except (KeyError, IndexError, TypeError, StopIteration) as exc:
        c.that(False, f"status line misses a field: {exc!r}")
    return ("FAIL" if c.failures else "PASS"), c.failures, total


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
