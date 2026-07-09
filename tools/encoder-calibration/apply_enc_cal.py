#!/usr/bin/env python3
"""Send the encoder ripple table to the firmware at runtime (no reflash).

Reads the per-wheel ratios produced by encoder_calib.py (/tmp/encoder_calib.json) and publishes
them as a Float32MultiArray of 72 floats (36 LEFT then 36 RIGHT) on /debug/enc_cal. The firmware
applies true_rpm = measured_rpm / CAL[bin] instantly (no lag).

WHY runtime (not compiled in): the encoder is incremental — its count resets to 0 at a random wheel
angle every boot, so a compiled-in table is phase-wrong after the flash that installs it. Loading the
table while the firmware is RUNNING (same boot as the calibration) keeps the phase correct. Re-run
this after any power-cycle (no reflash needed): encoder_calib.py, then this.

Usage:
    python3 apply_enc_cal.py                      # uses /tmp/encoder_calib.json
    python3 apply_enc_cal.py path/to/calib.json
    python3 apply_enc_cal.py --flat               # send all 1.0 (disable correction)
"""
import json
import sys
import time

import rclpy
from rclpy.node import Node
from std_msgs.msg import Float32MultiArray

NBINS = 36


def load_table(args):
    if "--flat" in args:
        return [1.0] * NBINS, [1.0] * NBINS
    paths = [a for a in args if not a.startswith("--")]
    path = paths[0] if paths else "/tmp/encoder_calib.json"
    d = json.load(open(path))
    left = list(d["left_ratio"])
    right = list(d["right_ratio"])
    if len(left) != NBINS or len(right) != NBINS:
        sys.exit(f"expected {NBINS} bins per wheel, got L={len(left)} R={len(right)}")
    # guard: replace non-finite / near-zero ratios with 1.0 (no correction at that bin)
    fix = lambda v: v if (v == v and v > 0.05) else 1.0
    return [fix(v) for v in left], [fix(v) for v in right]


def main():
    left, right = load_table(sys.argv[1:])
    rclpy.init()
    node = Node("apply_enc_cal")
    pub = node.create_publisher(Float32MultiArray, "/debug/enc_cal", 10)
    msg = Float32MultiArray()
    msg.data = [float(v) for v in left] + [float(v) for v in right]  # layout left empty on purpose

    pp = lambda x: (max(x) - min(x)) * 100
    print(f"sending table -> /debug/enc_cal  (LEFT ripple {pp(left):.0f}%  RIGHT ripple {pp(right):.0f}%)")
    # publish several times so the micro-ROS subscriber reliably gets it
    for i in range(20):
        pub.publish(msg)
        rclpy.spin_once(node, timeout_sec=0.05)
        time.sleep(0.1)
    print("done — firmware now applies the table (instant, no lag). Re-run after any reboot.")
    node.destroy_node()
    rclpy.shutdown()


if __name__ == "__main__":
    main()
