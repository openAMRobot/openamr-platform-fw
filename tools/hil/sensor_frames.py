#!/usr/bin/env python3
"""Read MB7060 and TF-Luna frames on the bench sensor taps and check their format.

Usage: sensor_frames.py [--mb7060 PORT ...] [--tfluna PORT ...] [--seconds 3]
Each port is a USB-UART tap on the sensor's TX line in the bench harness (MB7060 at 9600 8N1,
inverted RS232 levels converted by the tap; TF-Luna at 115200 8N1). A sensor passes when at least
3 complete frames arrive and, for TF-Luna, no frame has a bad checksum. Refuses without
HIL_BENCH=1. Reads only; the MB7060 trigger lines are not driven by this script.
"""
import argparse
import sys
import time

import hil_common as hil


def read(port_name, baud, seconds):
    import serial  # pyserial, bench runner only
    with serial.Serial(port_name, baud, timeout=0.1) as port:
        end, data = time.time() + seconds, b""
        while time.time() < end:
            data += port.read(512)
    return data


def main(argv=None):
    hil.require_bench("sensor_frames.py")
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--mb7060", nargs="*", default=[])
    p.add_argument("--tfluna", nargs="*", default=[])
    p.add_argument("--seconds", type=float, default=3)
    a = p.parse_args(argv)
    rc = 0
    if not a.mb7060 and not a.tfluna:
        return hil.record("sensor-frames", "NOT RUN", "no sensor tap ports given")
    for i, port in enumerate(a.mb7060):
        data = read(port, 9600, a.seconds)
        (hil.log_dir() / f"mb7060-{i}.bin").write_bytes(data)
        ranges = hil.parse_mb7060(data)
        rc |= hil.record(f"mb7060-{i}-frames", "PASS" if len(ranges) >= 3 else "FAIL",
                         f"{port}: {len(ranges)} frames, ranges {ranges[:5]} cm", hil.log_dir() / f"mb7060-{i}.bin")
    for i, port in enumerate(a.tfluna):
        data = read(port, 115200, a.seconds)
        (hil.log_dir() / f"tfluna-{i}.bin").write_bytes(data)
        frames, bad = hil.parse_tfluna(data)
        rc |= hil.record(f"tfluna-{i}-frames", "PASS" if len(frames) >= 3 and bad == 0 else "FAIL",
                         f"{port}: {len(frames)} frames, {bad} bad checksums", hil.log_dir() / f"tfluna-{i}.bin")
    return rc


if __name__ == "__main__":
    sys.exit(main())
