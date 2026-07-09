#!/usr/bin/env python3
"""Encoder calibration — characterize the per-revolution velocity error (magnet misalignment /
eccentricity). WHEELS IN THE AIR. 24V. Requires --arm.

Idea: at a CONSTANT open-loop PWM the wheel turns at a CONSTANT true speed. Over a FULL revolution
the encoder counts are exact (1024/rev), so the MEAN measured rpm = the true speed ("theoretical").
Binning the INSTANTANEOUS rpm by WHEEL ANGLE (counts mod CPR) reveals the error profile:
  ratio[angle] = sensor_rpm(angle) / mean_rpm.
If that profile has the SAME shape across speeds and the RIGHT wheel is flat, the LEFT error is
position-locked (geometric) = encoder misalignment, and the ratios ARE the correction:
  real_rpm = sensor_rpm / ratio[angle].

Usage:  python3 encoder_calib.py --arm [pwm1,pwm2,...]      default 120,180,250
"""
import math
import sys
import time
import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy
from geometry_msgs.msg import Twist, Vector3

CPR = 1024
NBINS = 36           # wheel-angle bins (10 deg each)
SETTLE = 3.0         # s to reach steady-state before recording
RECORD = 15.0        # s recorded per speed (longer -> smoother profile, esp. the right wheel)
PWM_LIMIT = 500.0
BE = QoSProfile(reliability=ReliabilityPolicy.BEST_EFFORT,
                history=HistoryPolicy.KEEP_LAST, depth=10)


class Cal(Node):
    def __init__(self):
        super().__init__('encoder_calib')
        self.olp = self.create_publisher(Vector3, '/debug/openloop', 10)
        self.cmd = self.create_publisher(Twist, '/cmd_vel', 10)
        self.create_subscription(Vector3, '/debug/left', self.cl, BE)
        self.create_subscription(Vector3, '/debug/right', self.cr, BE)
        self.rec = False
        self.L = []   # (counts, rpm)
        self.R = []
        self.seen = False

    def cl(self, m):
        self.seen = True
        if self.rec:
            self.L.append((m.z, m.y))

    def cr(self, m):
        if self.rec:
            self.R.append((m.z, m.y))

    def ol(self, pwm):
        v = Vector3(); v.x = float(pwm); self.olp.publish(v)
        self.cmd.publish(Twist())   # feed the watchdog (harmless; open-loop is checked first)

    def stop(self):
        for _ in range(15):
            self.ol(0.0); time.sleep(0.02)


def profile(samples):
    """Bin (counts, rpm) by wheel angle -> mean rpm per bin + overall mean."""
    bins = [[] for _ in range(NBINS)]
    for c, rpm in samples:
        b = int(c) % CPR * NBINS // CPR
        bins[b].append(rpm)
    means = [(sum(b) / len(b) if b else float('nan')) for b in bins]
    allr = [rpm for _, rpm in samples]
    mean = sum(allr) / len(allr) if allr else float('nan')
    return means, mean


def main():
    if '--arm' not in sys.argv:
        print("REFUSED: powered test needs --arm. Usage: encoder_calib.py --arm [pwm1,pwm2,...]")
        return
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    pwms = [float(x) for x in args[0].split(',')] if args else [120.0, 180.0, 250.0]
    pwms = [p for p in pwms if math.isfinite(p) and 0.0 < abs(p) <= PWM_LIMIT]
    if not pwms:
        print(f"REFUSED: no valid PWM (need finite, 0 < |pwm| <= {PWM_LIMIT}).")
        return

    rclpy.init()
    node = Cal()
    t = time.time()
    while time.time() - t < 2.0 and not node.seen:
        rclpy.spin_once(node, timeout_sec=0.05)
    if not node.seen:
        print("REFUSED: no /debug telemetry (agent/firmware down?). No motion sent.")
        node.destroy_node(); rclpy.shutdown(); return

    results = {}   # pwm -> (Lmeans, Lmean, Rmeans, Rmean)
    try:
        for pwm in pwms:
            print(f"\n=== PWM {pwm:.0f}: settling {SETTLE}s then recording {RECORD}s ...", flush=True)
            t = time.time()
            while time.time() - t < SETTLE:
                node.ol(pwm); rclpy.spin_once(node, timeout_sec=0.02)
            node.L.clear(); node.R.clear(); node.rec = True
            t = time.time()
            while time.time() - t < RECORD:
                node.ol(pwm); rclpy.spin_once(node, timeout_sec=0.02)
            node.rec = False
            Lm, Lmean = profile(list(node.L))
            Rm, Rmean = profile(list(node.R))
            results[pwm] = (Lm, Lmean, Rm, Rmean)
            print(f"   {len(node.L)} L / {len(node.R)} R samples | mean rpm  L={Lmean:.2f}  R={Rmean:.2f}")
            node.stop(); time.sleep(0.4)
    except KeyboardInterrupt:
        pass
    finally:
        node.stop()

    # --- table: relative error (sensor/mean) per wheel angle, LEFT, each speed ---
    print("\n===== LEFT encoder error profile = sensor_rpm / mean  (1.000 = perfect) =====")
    hdr = "angle |" + "".join(f"  PWM{int(p):>4}" for p in pwms)
    print(hdr); print("-" * len(hdr))
    for b in range(NBINS):
        ang = b * 360 // NBINS
        cells = ""
        for p in pwms:
            Lm, Lmean, _, _ = results[p]
            r = Lm[b] / Lmean if (Lmean and not math.isnan(Lm[b])) else float('nan')
            cells += f"   {r:5.3f}"
        print(f"{ang:4d}° |{cells}")

    for p in pwms:
        Lm, Lmean, Rm, Rmean = results[p]
        Lr = [m / Lmean for m in Lm if not math.isnan(m) and Lmean]
        Rr = [m / Rmean for m in Rm if not math.isnan(m) and Rmean]
        pp = lambda x: (max(x) - min(x)) * 100 if x else 0.0
        print(f"PWM {p:.0f}: ripple peak-peak  LEFT={pp(Lr):.1f}%   RIGHT={pp(Rr):.1f}%")

    print("\nConclusion:")
    print(" - LEFT profile SAME shape across PWMs + RIGHT ~flat -> position-locked geometric error")
    print("   = LEFT encoder magnet misalignment. The ratios above ARE the correction:")
    print("   real_rpm = sensor_rpm / ratio[angle].")
    print(" - LEFT ripple ~ RIGHT ripple, or shape changes with speed -> not a geometric encoder")
    print("   error (look at control/electrical/the left cable).")

    # --- save data for the firmware correction (averaged ratio across speeds = the correction) ---
    import json

    def clean(lst):
        return [None if (isinstance(x, float) and math.isnan(x)) else x for x in lst]

    def avg_ratio(left):
        out = []
        for b in range(NBINS):
            vals = []
            for p in pwms:
                Lm, Lmean, Rm, Rmean = results[p]
                m, mean = (Lm[b], Lmean) if left else (Rm[b], Rmean)
                if mean and not math.isnan(m):
                    vals.append(m / mean)
            out.append(round(sum(vals) / len(vals), 4) if vals else 1.0)
        return out

    data = {
        'nbins': NBINS, 'cpr': CPR, 'pwms': pwms,
        'left_ratio': avg_ratio(True),     # correction: real = sensor / ratio[bin]
        'right_ratio': avg_ratio(False),
        'per_speed': {str(p): {'left_mean': results[p][1], 'left_bins': clean(results[p][0]),
                               'right_mean': results[p][3], 'right_bins': clean(results[p][2])}
                      for p in pwms},
    }
    with open('/tmp/encoder_calib.json', 'w') as f:
        json.dump(data, f, indent=1)
    print("\ndata -> /tmp/encoder_calib.json  (averaged per-wheel ratios for the firmware correction)")

    # --- optional plot ---
    try:
        import matplotlib.pyplot as plt
        ang = [b * 360 / NBINS for b in range(NBINS)]
        fig, (a1, a2) = plt.subplots(2, 1, figsize=(9, 7), sharex=True)
        for p in pwms:
            Lm, Lmean, Rm, Rmean = results[p]
            a1.plot(ang, [m / Lmean if Lmean else float('nan') for m in Lm], marker='o', label=f'PWM {p:.0f}')
            a2.plot(ang, [m / Rmean if Rmean else float('nan') for m in Rm], marker='o', label=f'PWM {p:.0f}')
        for a, name in ((a1, 'LEFT'), (a2, 'RIGHT')):
            a.axhline(1.0, color='green', lw=1, alpha=0.6)
            a.set_ylabel(f'{name} sensor/mean'); a.grid(True, alpha=0.3); a.legend(fontsize=8)
        a2.set_xlabel('wheel angle (deg, counts mod 1024)')
        a1.set_title('Encoder velocity error vs wheel angle (flat green = perfect)')
        plt.tight_layout(); plt.savefig('/tmp/encoder_calib.png'); print("\nplot -> /tmp/encoder_calib.png")
        plt.show()
    except Exception as e:
        print(f"(no plot: {e})")

    node.destroy_node(); rclpy.shutdown()


main()
