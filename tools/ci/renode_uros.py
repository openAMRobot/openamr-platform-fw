#!/usr/bin/env python3
"""Run the TelemetryUrosSerialSimBench image in Renode against a micro-ROS agent and check its topics.

SIMULATION ONLY, not evidence for a board. The image's USART3 is exposed by Renode as a
pseudo-terminal; the micro-ROS agent (micro_ros_agent serial) on the host connects to it, and a
ROS 2 node of this script (rclpy, run with ROS 2 Jazzy sourced) subscribes to the image's topics.
The drive, lift and BMS are the stubs of tools/ci/renode_drive_stub.py; a TF-Luna stub feeds
USART2. Renode is throttled to wall time (tlm_throttle_start), because the agent runs in wall
time; the measured ratio of virtual to wall time is printed.

Phases, in one Renode process:
  connect        the client reaches RUN: every topic appears with its type
  nominal        rates (one message per topic per report period, counted against the status
                 sequence), values (wheel 1234 and -1234 drive units, front range 1.5 m, rear
                 NaN), no status sequence gap beyond best-effort loss
  stopped_drive  the drive stub goes silent: wheel speeds become NaN (never 0) and the drive
                 heartbeat "lost"
  agent_restart  the agent is stopped for 5 s and started again: the client tears down
                 (losses 1), reconnects (sessions 2), topics resume, and the CAN tick and sensor
                 poll report zero missed deadlines throughout (tick_miss, sens_miss)

Usage: renode_uros.py --renode RENODE --elf TelemetryUrosSerialSimBench.elf --agent MICRO_ROS_AGENT
                      [--out DIR] [--summary FILE]
Exit 0 all phases passed, 1 a phase failed, 2 blocked (Renode, the agent or rclpy missing).
"""
import argparse
import json
import math
import os
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ADDITIONS = ROOT / "tests" / "renode" / "stm32h723-additions.repl"
STUB = Path(__file__).resolve().parent / "renode_drive_stub.py"
NS = "/base_fw"
TOPICS = {
    NS + "/wheel_speeds": "std_msgs/msg/Float32MultiArray",
    NS + "/drive_heartbeat": "std_msgs/msg/String",
    NS + "/tof/front": "sensor_msgs/msg/Range",
    NS + "/tof/rear": "sensor_msgs/msg/Range",
    NS + "/status": "std_msgs/msg/String",
}
PERIOD_S = 0.1
THROTTLE_SLICE_MS = 5


def resc(elf, pty):
    return "\n".join([
        'mach create "openamr"',
        "machine LoadPlatformDescription @platforms/cpus/stm32h743.repl",
        f"machine LoadPlatformDescription @{ADDITIONS}",
        'sysbus Tag <0x58024804, 0x58024807> "CSR1" 0x2000',
        "macro reset", '"""', f"    sysbus LoadELF @{elf}", "    cpu VectorTableOffset 0x08000000", '"""',
        "runMacro $reset",
        f'emulation CreateUartPtyTerminal "uros_pty" "{pty}" true',
        "connector Connect sysbus.usart3 uros_pty",
        'emulation CreateCANHub "can1"', "connector Connect sysbus.fdcan1 can1",
        'emulation CreateCANHub "can2"', "connector Connect sysbus.fdcan2 can2",
        'emulation CreateCANHub "can3"', "connector Connect sysbus.fdcan3 can3",
        f"include @{STUB}",
        "logLevel 3",
        'tlm_stub_attach "can1"',
        'tlm_aux_attach "can2" "can3"',
        'tlm_stub_tfluna "sysbus.usart2" 10 150 500',
        f"tlm_throttle_start {THROTTLE_SLICE_MS}",
        "start",
    ]) + "\n"


class Renode:
    def __init__(self, renode, elf, out):
        self.pty = str(out / "usart3.pty")
        if os.path.lexists(self.pty):
            os.unlink(self.pty)
        script = out / "uros.resc"
        script.write_text(resc(Path(elf).resolve(), self.pty))
        self.log = open(out / "uros.renode.log", "w", encoding="utf-8")
        self.proc = subprocess.Popen([renode, "--disable-gui", "--console"], stdin=subprocess.PIPE, stdout=self.log,
                                     stderr=subprocess.STDOUT, text=True, start_new_session=True)
        self.monitor(f"include @{script}")
        self.out = out

    def monitor(self, line):
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()

    def time_report(self):
        self.monitor("tlm_time_report")
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            self.log.flush()
            for line in reversed((self.out / "uros.renode.log").read_text(errors="replace").splitlines()):
                if "TLM_TIME {" in line:
                    return json.loads(line[line.index("{"):])
            time.sleep(0.2)
        return None

    def stop(self):
        try:
            self.monitor("quit")
            self.proc.wait(timeout=20)
        except (OSError, ValueError, subprocess.TimeoutExpired):
            os.killpg(self.proc.pid, signal.SIGKILL)
            self.proc.wait(timeout=5)
        self.log.close()


class Agent:
    def __init__(self, agent, pty, out, n):
        self.log = open(out / f"agent-{n}.log", "w", encoding="utf-8")
        self.proc = subprocess.Popen([agent, "serial", "--dev", pty, "-b", "115200"], stdout=self.log,
                                     stderr=subprocess.STDOUT, start_new_session=True)

    def stop(self):
        if self.proc.poll() is None:
            os.killpg(self.proc.pid, signal.SIGINT)
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(self.proc.pid, signal.SIGKILL)
                self.proc.wait(timeout=5)
        self.log.close()


class Recorder:
    """rclpy node: every message of the five topics with its wall receipt time."""

    def __init__(self):
        import rclpy
        from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy
        from sensor_msgs.msg import Range
        from std_msgs.msg import Float32MultiArray, String
        self.rclpy = rclpy
        rclpy.init()
        self.node = rclpy.create_node("renode_uros_check")
        qos = QoSProfile(depth=50, reliability=ReliabilityPolicy.BEST_EFFORT, history=HistoryPolicy.KEEP_LAST)
        self.lock = threading.Lock()
        self.msgs = {t: [] for t in TOPICS}
        types = {"std_msgs/msg/Float32MultiArray": Float32MultiArray, "std_msgs/msg/String": String,
                 "sensor_msgs/msg/Range": Range}
        for topic, tname in TOPICS.items():
            self.node.create_subscription(types[tname], topic, self._cb(topic), qos)
        self.executor = rclpy.executors.SingleThreadedExecutor()
        self.executor.add_node(self.node)
        self.thread = threading.Thread(target=self.executor.spin, daemon=True)
        self.thread.start()

    def _cb(self, topic):
        def cb(msg):
            with self.lock:
                self.msgs[topic].append((time.monotonic(), msg))
        return cb

    def since(self, topic, t0):
        with self.lock:
            return [m for (t, m) in self.msgs[topic] if t >= t0]

    def status_since(self, t0):
        out = []
        for m in self.since(NS + "/status", t0):
            try:
                out.append(json.loads(m.data))
            except ValueError:
                pass
        return out

    def topic_types(self):
        return {name: types for name, types in self.node.get_topic_names_and_types()}

    def stop(self):
        self.executor.shutdown()
        self.node.destroy_node()
        self.rclpy.shutdown()


class Check:
    def __init__(self):
        self.failures = []

    def that(self, ok, msg):
        if not ok:
            self.failures.append(msg)
        return ok


def wait_for(pred, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if pred():
            return True
        time.sleep(0.2)
    return False


def isnan(x):
    return isinstance(x, float) and math.isnan(x)


def phase_rates(c, rec, t0, t1):
    st = rec.status_since(t0)
    st = [s for s in st if s.get("uros") == "run"]
    if not c.that(len(st) >= 20, f"{len(st)} status messages in the nominal window"):
        return {}
    seqs = [s["seq"] for s in st]
    span = seqs[-1] - seqs[0] + 1
    up_span_s = (st[-1]["up_us"] - st[0]["up_us"]) / 1e6
    rate_virtual = (len(st) - 1) / up_span_s if up_span_s > 0 else 0.0
    lost = span - len(st)
    c.that(lost <= max(1, span // 20), f"status: {lost} of {span} periods missing (best effort, more than 5 %)")
    c.that(abs(rate_virtual - 1 / PERIOD_S) <= 1.0, f"status rate {rate_virtual:.2f}/s of virtual time, expected 10")
    counts = {t: len([m for (tt, m) in rec.msgs[t] if t0 <= tt <= t1]) for t in TOPICS}
    for t, n in counts.items():
        c.that(abs(n - counts[NS + "/status"]) <= max(2, counts[NS + "/status"] // 10),
               f"{t}: {n} messages against {counts[NS + '/status']} status messages")
    return {"status_rate_virtual_hz": round(rate_virtual, 2), "status_lost": lost, "counts": counts}


def phase_values(c, rec, t0):
    wheels = rec.since(NS + "/wheel_speeds", t0)
    if c.that(wheels, "no wheel speed message"):
        last = list(wheels[-1].data)
        c.that(last == [1234.0, -1234.0], f"wheel speeds {last}, expected [1234, -1234] drive units")
        c.that(wheels[-1].layout.dim and wheels[-1].layout.dim[0].label == "ch1_ch2_drive_units",
               "wheel speed layout label")
    hb = rec.since(NS + "/drive_heartbeat", t0)
    c.that(hb and hb[-1].data == "alive", f"drive heartbeat {hb[-1].data if hb else None}")
    front = rec.since(NS + "/tof/front", t0)
    rear = rec.since(NS + "/tof/rear", t0)
    if c.that(front and rear, "no range message"):
        f, r = front[-1], rear[-1]
        c.that(abs(f.range - 1.5) < 1e-3 and f.header.frame_id == "tof_front" and f.radiation_type == 1
               and abs(f.min_range - 0.2) < 1e-6 and abs(f.max_range - 8.0) < 1e-6, f"front range {f}")
        c.that(isnan(r.range) and r.header.frame_id == "tof_rear", f"rear range {r.range} (no frame: NaN expected)")
    st = rec.status_since(t0)
    if c.that(st, "no status message"):
        s = st[-1]
        c.that(s.get("params") == "test" and isinstance(s.get("commit"), str) and len(s["commit"]) >= 7,
               f"status build fields {s}")
        c.that(s["lift"]["st"] == "IDLE" and s["lift"]["pos"] == 5000 and s["bms"] == "locked", f"status lift/bms {s}")


def phase_stopped(c, rec, t0):
    wheels = rec.since(NS + "/wheel_speeds", t0)
    hb = rec.since(NS + "/drive_heartbeat", t0)
    if not c.that(len(wheels) >= 5 and hb, f"{len(wheels)} wheel messages after the drive stopped"):
        return
    tail = [list(m.data) for m in wheels[-5:]]
    c.that(all(isnan(v) for row in tail for v in row), f"stopped drive: wheel speeds {tail}, expected NaN")
    c.that(not any(v == 0.0 for row in tail for v in row), "a stopped drive was published as 0")
    c.that(hb[-1].data == "lost", f"drive heartbeat {hb[-1].data} after the drive stopped")


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--renode", required=True)
    p.add_argument("--elf", required=True)
    p.add_argument("--agent", required=True, help="micro_ros_agent executable")
    p.add_argument("--out", default="build/renode-uros")
    p.add_argument("--summary")
    a = p.parse_args(argv)
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    for path in (a.renode, a.elf, a.agent):
        if not os.path.exists(path):
            print(f"BLOCKED: {path} does not exist")
            print("Renode micro-ROS phases: 0 passed, 0 failed, 1 blocked, of 4")
            return 2
    # The image's participant is on DDS domain 0 (rclc_support_init default; the client takes no
    # domain parameter): the recorder must be there too, whatever the shell sets.
    os.environ["ROS_DOMAIN_ID"] = "0"
    try:
        rec = Recorder()
    except ImportError as exc:
        print(f"BLOCKED: rclpy not available ({exc}); source ROS 2 Jazzy first")
        print("Renode micro-ROS phases: 0 passed, 0 failed, 1 blocked, of 4")
        return 2
    results, report = [], {}
    renode = Renode(a.renode, a.elf, out)
    agents = []
    t_wall0 = time.monotonic()
    try:
        if not wait_for(lambda: os.path.exists(renode.pty), 60):
            print("BLOCKED: Renode created no pseudo-terminal for USART3")
            print("Renode micro-ROS phases: 0 passed, 0 failed, 1 blocked, of 4")
            return 2
        agents.append(Agent(a.agent, renode.pty, out, 1))

        c = Check()
        ok = wait_for(lambda: any(s.get("uros") == "run" for s in rec.status_since(t_wall0)), float(os.environ.get("UROS_CONNECT_S", "90")))
        c.that(ok, "no status message from the client within 90 s")
        if ok:
            wait_for(lambda: all(t in rec.topic_types() for t in TOPICS), 15)
            types = rec.topic_types()
            for t, tname in TOPICS.items():
                c.that(tname in types.get(t, []), f"topic {t} with type {tname}: seen {types.get(t)}")
            extra = [t for t in types if t.startswith(NS + "/") and t not in TOPICS]
            c.that(not extra, f"topics that are not in the list: {extra}")
        results.append(("connect", c))

        c = Check()
        t0 = time.monotonic()
        time.sleep(6.0)
        t1 = time.monotonic()
        report["nominal"] = phase_rates(c, rec, t0, t1)
        phase_values(c, rec, t0)
        results.append(("nominal", c))

        c = Check()
        renode.monitor('tlm_stub_set "silent" 1')
        time.sleep(3.0)
        phase_stopped(c, rec, time.monotonic() - 1.0)
        renode.monitor('tlm_stub_set "silent" 0')
        results.append(("stopped_drive", c))

        c = Check()
        time.sleep(2.0)
        before = rec.status_since(t_wall0)
        agents[-1].stop()
        t_stop = time.monotonic()
        time.sleep(5.0)
        agents.append(Agent(a.agent, renode.pty, out, 2))
        t_restart = time.monotonic()
        resumed = wait_for(lambda: any(s.get("uros") == "run" and s.get("sessions", 0) >= 2
                                       for s in rec.status_since(t_restart)), 60)
        c.that(resumed, "topics did not resume within 60 s of the agent restart")
        if resumed:
            time.sleep(3.0)
            after = rec.status_since(t_restart)
            last = after[-1]
            c.that(last["sessions"] == 2 and last["losses"] == 1, f"client sessions {last['sessions']} losses {last['losses']}")
            c.that(last["tick_miss"] == 0 and last["sens_miss"] == 0,
                   f"missed deadlines across the agent restart: tick {last['tick_miss']} sensors {last['sens_miss']}")
            gap_up = [s["up_us"] for s in after]
            c.that(before and gap_up and gap_up[0] > before[-1]["up_us"], "status uptime does not continue after the restart")
            for t in TOPICS:
                c.that(len(rec.since(t, t_restart)) >= 10, f"{t}: {len(rec.since(t, t_restart))} messages after the restart")
            report["agent_restart"] = {"outage_s": round(t_restart - t_stop, 2),
                                       "resume_after_s": round(min(tt for tt, _ in rec.msgs[NS + "/status"] if tt >= t_restart) - t_restart, 2)}
        results.append(("agent_restart", c))

        last = rec.status_since(t_wall0)
        if last:
            # arena: bytes of the static micro-ROS arena ever used; stk: reporter stack words never used
            report["last_status"] = last[-1]
        tr = renode.time_report()
        if tr and tr["wall_s"] > 0:
            report["virtual_to_wall"] = round(tr["virtual_s"] / tr["wall_s"], 3)
            report["time"] = tr
    finally:
        for ag in agents:
            ag.stop()
        renode.stop()
        rec.stop()

    passed = sum(1 for _, c in results if not c.failures)
    failed = len(results) - passed
    for name, c in results:
        print(f"{'PASS' if not c.failures else 'FAIL'} {name}")
        for m in c.failures[:12]:
            print("    " + m)
    print("Measurements: " + json.dumps(report))
    print(f"Renode micro-ROS phases: {passed} passed, {failed} failed, 0 blocked, of {len(results)}")
    (out / "report.json").write_text(json.dumps({"results": {n: c.failures for n, c in results}, "report": report}, indent=2))
    if a.summary:
        with open(a.summary, "a", encoding="utf-8") as fh:
            fh.write("### Renode micro-ROS phases (serial transport, simulation only)\n\n| Phase | Result |\n|---|---|\n")
            for name, c in results:
                fh.write(f"| {name} | {'PASS' if not c.failures else 'FAIL: ' + c.failures[0]} |\n")
            fh.write("\n```\n" + json.dumps(report, indent=2) + "\n```\n")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
