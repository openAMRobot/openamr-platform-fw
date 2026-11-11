#!/usr/bin/env python3
"""Write the bench record (bench-record.schema.json) from the automated results of a bench run.

Usage: write_bench_record.py --out bench-record.json --firmware-sha SHA --hardware-revision REV
       --bom-issue ISSUE --p03-revision REV --fixture-revision REV --operator LOGIN --run-url URL
       [--image-sha256 HEX] [--results build/hil/results.jsonl] [--logs build/hil/logs]
The manual checklist is written with every item PENDING and no signature: the operator performs
the checks in docs/hil-bench.md, fills in each item and signs. Validates against the schema.
This script runs anywhere (it reads files only); it is not a bench script.
"""
import argparse
import datetime
import json
import sys
from pathlib import Path

import jsonschema

HERE = Path(__file__).resolve().parent
MANUAL_CHECKLIST = [
    "E-stop: each E-stop button removes actuator power (traction, arms, lift) and engages the brakes; Jetson, 5 V and regulated 24 V stay powered",
    "RESET: short press performs the PNOZ monitored reset only after the E-stop is released; motion still needs a command",
    "RESET: 5 s hold requests the Jetson reboot, then the controller resets itself",
    "Brake path: brakes engage with actuator power removed and release only with actuator power present",
    "Lift power path: lift power is removed by the E-stop and cannot be restored by firmware",
    "Watchdog: stopping /cmd_vel stops the wheels within the configured timeout (observed, not changed)",
    "Communication loss: unplugging Ethernet stops motion; the controller reports the loss",
    "Charger interlock: with the charge relay energised, actuators cannot be enabled; a RESET is needed after charging",
    "Fault injection: BMS fault frame on CAN2 stops motion and locks; sensor disconnect is reported",
    "Y32, E-stop spare NO, RESET NC and charger fault inputs are reported with the right state",
]


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    for name in ("out", "firmware-sha", "hardware-revision", "bom-issue", "p03-revision", "fixture-revision",
                 "operator", "run-url"):
        p.add_argument(f"--{name}", required=True)
    p.add_argument("--image-sha256")
    p.add_argument("--results", default="build/hil/results.jsonl")
    p.add_argument("--logs", default="build/hil/logs")
    a = p.parse_args(argv)
    results = Path(a.results)
    tests = [json.loads(l) for l in results.read_text(encoding="utf-8").splitlines() if l.strip()] if results.is_file() else []
    if not tests:
        tests = [{"test": "bench", "status": "NOT RUN", "detail": "no automated result was recorded", "log": None}]
    logs = sorted(str(p) for p in Path(a.logs).rglob("*") if p.is_file()) if Path(a.logs).is_dir() else []
    record = {
        "schema_version": 1,
        "firmware_sha": a.firmware_sha,
        "hardware_revision": a.hardware_revision,
        "bom_issue": a.bom_issue,
        "p03_revision": a.p03_revision,
        "fixture_revision": a.fixture_revision,
        "operator": a.operator,
        "date": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "run_url": a.run_url,
        "tests": tests,
        "raw_logs": logs,
        "manual_checklist": [{"item": item, "status": "PENDING"} for item in MANUAL_CHECKLIST],
        "signature": None,
    }
    if a.image_sha256:
        record["firmware_image_sha256"] = a.image_sha256
    jsonschema.Draft202012Validator(json.loads((HERE / "bench-record.schema.json").read_text())).validate(record)
    Path(a.out).write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    failed = sum(t["status"] != "PASS" for t in tests)
    print(f"{a.out}: {len(tests)} automated results ({failed} not PASS), {len(MANUAL_CHECKLIST)} manual items PENDING, unsigned")
    return 0


if __name__ == "__main__":
    sys.exit(main())
