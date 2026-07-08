#!/usr/bin/env python3
"""FAST per-boot encoder calibration = place the known table (no full re-measure).

The ripple SHAPE (magnet geometry) never changes — only its PHASE shifts each boot (incremental
encoder zeroes at a random wheel angle). So we don't remeasure the table; we do a SHORT spin just to
find the phase offset, then roll the stored reference table to the current frame and load it.

~6 s instead of ~54 s. WHEELS IN THE AIR. 24V. Requires --arm.

Usage:  python3 align_enc_cal.py --arm [pwm]      default 250
        python3 align_enc_cal.py --arm --record 3 250
Reference shape: scripts/encoder_ref_table.json (regenerate with a full encoder_calib.py if the
magnet is ever physically disturbed).
"""
import json
import math
import os
import sys
import time

import numpy as np
import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy
from geometry_msgs.msg import Twist, Vector3
from std_msgs.msg import Float32MultiArray

CPR = 1024
NBINS = 36
SETTLE = 1.5
RECORD = 6.0          # ~2-3 revs at PWM 250 -> cleaner profile -> sharper sub-bin phase lock
PWM_LIMIT = 500.0
REF = os.path.join(os.path.dirname(__file__), 'encoder_ref_table.json')
BE = QoSProfile(reliability=ReliabilityPolicy.BEST_EFFORT,
                history=HistoryPolicy.KEEP_LAST, depth=10)


class Align(Node):
    def __init__(self):
        super().__init__('align_enc_cal')
        self.olp = self.create_publisher(Vector3, '/debug/openloop', 10)
        self.cmd = self.create_publisher(Twist, '/cmd_vel', 10)
        self.calp = self.create_publisher(Float32MultiArray, '/debug/enc_cal', 10)
        self.create_subscription(Vector3, '/debug/left', self.cl, BE)
        self.create_subscription(Vector3, '/debug/right', self.cr, BE)
        self.rec = False
        self.L, self.R, self.seen = [], [], False

    def cl(self, m):
        self.seen = True
        if self.rec:
            self.L.append((m.z, m.y))

    def cr(self, m):
        if self.rec:
            self.R.append((m.z, m.y))

    def ol(self, pwm):
        v = Vector3(); v.x = float(pwm); self.olp.publish(v)
        self.cmd.publish(Twist())

    def stop(self):
        for _ in range(15):
            self.ol(0.0); time.sleep(0.02)


def profile(samples):
    """Mean rpm per wheel-angle bin (normalized to overall mean). NaN bins -> filled by neighbours."""
    bins = [[] for _ in range(NBINS)]
    for c, rpm in samples:
        bins[int(c) % CPR * NBINS // CPR].append(rpm)
    allr = [rpm for _, rpm in samples]
    mean = sum(allr) / len(allr) if allr else float('nan')
    prof = np.array([(sum(b) / len(b) / mean) if (b and mean) else np.nan for b in bins])
    if np.isnan(prof).any():                       # fill gaps so correlation is clean
        idx = np.arange(NBINS)
        good = ~np.isnan(prof)
        prof = np.interp(idx, idx[good], prof[good], period=NBINS) if good.any() else np.ones(NBINS)
    return prof


def best_shift(cur, ref, up=20):
    """Find the SUB-BIN circular offset (cur[b] ~ ref[b-offset]) and return (offset_deg, placed_table).

    Both profiles are upsampled x`up` (-> 0.5 deg resolution) before correlating, so the table is
    placed far finer than the 10 deg bin grid. The 36-bin table is then resampled from the reference
    at the fractional offset. This is what brings the residual down (a 1-bin/10 deg miss leaves a big
    residual on a 2/rev ripple)."""
    x = np.arange(NBINS)
    xf = np.linspace(0.0, NBINS, NBINS * up, endpoint=False)
    cf = np.interp(xf, x, cur, period=NBINS); cf -= cf.mean()
    rf = np.interp(xf, x, ref, period=NBINS); rf -= rf.mean()
    scores = [float(np.dot(cf, np.roll(rf, s))) for s in range(NBINS * up)]
    s = int(np.argmax(scores))
    offset_bins = s / up                                   # fractional bins
    placed = np.interp((x - offset_bins) % NBINS, x, ref, period=NBINS)
    return offset_bins * 360.0 / NBINS, placed


def main():
    if '--arm' not in sys.argv:
        print("REFUSED: powered test needs --arm. Usage: align_enc_cal.py --arm [pwm]")
        return
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    record = RECORD
    if '--record' in sys.argv:
        i = sys.argv.index('--record')
        record = float(sys.argv[i + 1]); args = [a for a in args if a != sys.argv[i + 1]]
    pwm = float(args[0]) if args else 250.0
    if not (math.isfinite(pwm) and 0.0 < abs(pwm) <= PWM_LIMIT):
        print(f"REFUSED: bad PWM (need 0 < |pwm| <= {PWM_LIMIT}).")
        return

    ref = json.load(open(REF))
    refL, refR = np.array(ref['left_ratio']), np.array(ref['right_ratio'])

    rclpy.init()
    node = Align()
    t = time.time()
    while time.time() - t < 2.0 and not node.seen:
        rclpy.spin_once(node, timeout_sec=0.05)
    if not node.seen:
        print("REFUSED: no /debug telemetry (agent/firmware down?). No motion sent.")
        node.destroy_node(); rclpy.shutdown(); return

    # CRITICAL: clear any table already loaded so we measure the RAW ripple, not a residual.
    # (Re-running align over a loaded table would correlate corrected data -> wrong offset.)
    flat = Float32MultiArray(); flat.data = [1.0] * (2 * NBINS)
    print("resetting firmware table to flat (measure raw ripple)...", flush=True)
    t = time.time()
    while time.time() - t < 1.0:
        node.calp.publish(flat); rclpy.spin_once(node, timeout_sec=0.05); time.sleep(0.05)

    try:
        print(f"spin PWM {pwm:.0f}: settle {SETTLE}s + record {record}s (finding phase only)...", flush=True)
        t = time.time()
        while time.time() - t < SETTLE:
            node.ol(pwm); rclpy.spin_once(node, timeout_sec=0.02)
        node.L.clear(); node.R.clear(); node.rec = True
        t = time.time()
        while time.time() - t < record:
            node.ol(pwm); rclpy.spin_once(node, timeout_sec=0.02)
        node.rec = False
    finally:
        node.stop()

    if len(node.L) < NBINS or len(node.R) < NBINS:
        print(f"REFUSED: too few samples (L={len(node.L)} R={len(node.R)}) — wheels didn't spin?")
        node.destroy_node(); rclpy.shutdown(); return

    curL, curR = profile(list(node.L)), profile(list(node.R))
    degL, tabL = best_shift(curL, refL)
    degR, tabR = best_shift(curR, refR)
    print(f"phase offset found:  LEFT {degL:.1f}°   RIGHT {degR:.1f}°  "
          f"({len(node.L)} L / {len(node.R)} R samples)")

    msg = Float32MultiArray()
    msg.data = [float(v) for v in tabL] + [float(v) for v in tabR]
    for _ in range(20):
        node.calp.publish(msg); rclpy.spin_once(node, timeout_sec=0.05); time.sleep(0.08)
    print("table placed -> /debug/enc_cal. Correction active (instant, no lag). Re-run after a power-cycle.")

    node.destroy_node(); rclpy.shutdown()


main()
