"""Shared helpers for the bench scripts in tools/hil (layer 3, hardware-in-the-loop).

Every script calls require_bench() first: it refuses to run unless HIL_BENCH=1 is set on the
runner, so nothing here can touch hardware from a developer laptop or a GitHub-hosted runner.

None of these scripts drives the E-stop chain, contactors, brakes, lift power or the charger,
and none sends a motion command. The checklist in docs/hil-bench.md covers those paths; a human
performs and signs it on every run.

Result lines are JSON objects appended to $HIL_RESULTS (default build/hil/results.jsonl):
{"test": name, "status": "PASS"|"FAIL"|"NOT RUN", "detail": text, "log": path}.
The pure parsers below (banner, MB7060, TF-Luna) are unit-tested on the host in tests/hil.
"""
import json
import os
import re
import sys
from pathlib import Path

REFUSED = 3  # exit code when HIL_BENCH is not set


def require_bench(script):
    if os.environ.get("HIL_BENCH") != "1":
        print(f"REFUSED: {script} runs only on the bench runner with HIL_BENCH=1 set; nothing was done.")
        sys.exit(REFUSED)


def results_path():
    return Path(os.environ.get("HIL_RESULTS", "build/hil/results.jsonl"))


def log_dir():
    d = Path(os.environ.get("HIL_LOG_DIR", "build/hil/logs"))
    d.mkdir(parents=True, exist_ok=True)
    return d


def record(test, status, detail, log=None):
    assert status in ("PASS", "FAIL", "NOT RUN"), status
    path = results_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "a", encoding="utf-8") as fh:
        fh.write(json.dumps({"test": test, "status": status, "detail": detail, "log": str(log) if log else None}) + "\n")
    print(f"{status} {test}: {detail}")
    return 0 if status == "PASS" else 1


# Boot banner on the ST-LINK virtual COM port (USART3, PD8/PD9). Proposed firmware contract, not
# implemented by the firmware yet (docs/hil-bench.md):
#   OPENAMR BOOT fw=<git sha> board=NUCLEO-H723ZG
#   OPENAMR RESET reason=<POR|PIN|SFT|IWDG|WWDG|BOR|LPWR>[,...]
#   OPENAMR IMU whoami=0x47
BANNER = re.compile(r"^OPENAMR BOOT fw=([0-9a-f]{7,40}) board=(\S+)\s*$")
RESET = re.compile(r"^OPENAMR RESET reason=([A-Z,]+)\s*$")
WHOAMI = re.compile(r"^OPENAMR IMU whoami=(0x[0-9A-Fa-f]{2})\s*$")
RESET_REASONS = {"POR", "PIN", "SFT", "IWDG", "WWDG", "BOR", "LPWR", "CPU", "D1", "D2"}


def parse_banner(lines):
    out = {}
    for line in lines:
        line = line.strip()
        if m := BANNER.match(line):
            out["fw"], out["board"] = m.group(1), m.group(2)
        elif m := RESET.match(line):
            out["reset"] = m.group(1).split(",")
        elif m := WHOAMI.match(line):
            out["whoami"] = int(m.group(1), 16)
    return out


def parse_mb7060(data):
    """MB7060 serial output (datasheet 11838, pin 5): 'R', range digits in cm, CR, 9600 8N1.
    Returns the ranges in cm of the complete frames in data (bytes)."""
    return [int(m.group(1)) for m in re.finditer(rb"R(\d{3,4})\r", data)]


def parse_tfluna(data):
    """TF-Luna default 9-byte/cm frame (user manual, Appendix I): 0x59 0x59 Dist_L Dist_H Amp_L
    Amp_H Temp_L Temp_H Checksum, checksum = low 8 bits of the sum of bytes 0..7.
    Returns (frames, bad_checksums); frames are (dist_cm, amp, temp_c)."""
    frames, bad, i = [], 0, 0
    while i + 9 <= len(data):
        if data[i] == 0x59 and data[i + 1] == 0x59:
            frame = data[i:i + 9]
            if sum(frame[:8]) & 0xFF == frame[8]:
                dist = frame[2] | frame[3] << 8
                amp = frame[4] | frame[5] << 8
                temp = (frame[6] | frame[7] << 8) / 8 - 256
                frames.append((dist, amp, temp))
                i += 9
                continue
            bad += 1
        i += 1
    return frames, bad
