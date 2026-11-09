# SPDX-License-Identifier: MIT
"""End-to-end software-in-the-loop harness.

Chain per test (fresh processes, so no state leaks between scenarios):

    rclpy test node --/cmd_vel--> micro-ROS agent --pty--> oamr_base_host
        --vcan0--> ZLAC8015D emulator (in-process, tools/can_sil)
        --vcan1--> Daly BMS emulator (in-process, tools/can_sil)
    and back: /base/status, /base/aux_status, /odom/unfiltered, /imu/data_raw.

Needs: ROS 2 Jazzy, a built oamr_base_host (OAMR_BASE_HOST, default
build/host/oamr_base_host), the micro-ROS agent (MICRO_ROS_AGENT_SETUP, a
setup.bash that provides `ros2 run micro_ros_agent micro_ros_agent`), and
permission to create vcan interfaces (root or passwordless sudo).
tools/sil_setup.sh prepares all of it; tools/sil_test.sh runs this suite.
"""
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time

import pytest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "can_sil"))

import can  # noqa: E402
import rclpy  # noqa: E402
from rclpy.node import Node  # noqa: E402
from rclpy.qos import (DurabilityPolicy, QoSProfile, ReliabilityPolicy,  # noqa: E402
                       HistoryPolicy, qos_profile_sensor_data)
from diagnostic_msgs.msg import DiagnosticStatus  # noqa: E402
from geometry_msgs.msg import Twist  # noqa: E402
from nav_msgs.msg import Odometry  # noqa: E402
from sensor_msgs.msg import Imu  # noqa: E402

from zlac8015d_emulator import Zlac8015dEmulator  # noqa: E402
from daly_bms_emulator import DalyBmsEmulator  # noqa: E402

BASE_BIN = os.environ.get("OAMR_BASE_HOST", os.path.join(REPO, "build", "host", "oamr_base_host"))
AGENT_SETUP = os.environ.get("MICRO_ROS_AGENT_SETUP",
                             os.path.expanduser("~/uros/install/local_setup.bash"))
COMMISSIONING = os.path.join(REPO, "boards", "host", "config", "sil_commissioning.conf")
CAN1 = os.environ.get("SIL_CAN1", "vcan0")
CAN2 = os.environ.get("SIL_CAN2", "vcan1")
SIL_LOG_DIR = os.environ.get("SIL_LOG_DIR")


def sh(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True)


def priv(cmd):
    return sh(("" if os.geteuid() == 0 else "sudo -n ") + cmd)


def vcan_up(name):
    if sh("ip link show %s" % name).returncode != 0:
        priv("modprobe vcan")
        r = priv("ip link add dev %s type vcan" % name)
        if r.returncode != 0:
            return False
    priv("ip link set up %s" % name)
    return "UP" in sh("ip -br link show %s" % name).stdout or \
        "UNKNOWN" in sh("ip -br link show %s" % name).stdout


def vcan_delete(name):
    return priv("ip link delete %s" % name).returncode == 0


@pytest.fixture(scope="session")
def prerequisites():
    if not os.path.exists(BASE_BIN):
        pytest.skip("oamr_base_host not built (%s); run tools/sil_setup.sh" % BASE_BIN)
    if not os.path.exists(AGENT_SETUP):
        pytest.skip("micro-ROS agent setup not found (%s)" % AGENT_SETUP)
    for name in (CAN1, CAN2):
        if not vcan_up(name):
            pytest.skip("cannot create %s (needs root or passwordless sudo)" % name)
    rclpy.init()
    yield
    rclpy.shutdown()


def kv(msg):
    return {v.key: v.value for v in msg.values}


def fault_codes(status):
    text = status.get("faults", "")
    return [int(f.split(":")[0]) for f in text.split(";") if f]


class Observer(Node):
    """rclpy side of the chain: publishes /cmd_vel, records every topic."""

    def __init__(self):
        super().__init__("sil_observer")
        status_qos = QoSProfile(depth=1, reliability=ReliabilityPolicy.RELIABLE,
                                durability=DurabilityPolicy.TRANSIENT_LOCAL,
                                history=HistoryPolicy.KEEP_LAST)
        self.lock = threading.Lock()
        self.status = None
        self.status_rx = None       # monotonic time of the last /base/status
        self.status_count = 0
        self.status_times = []
        self.aux = None
        self.aux_times = []
        self.odom = None
        self.odom_count = 0
        self.odom_max_v = 0.0
        self.imu_count = 0
        self.cmd_pub = self.create_publisher(Twist, "/cmd_vel", 10)
        self.create_subscription(DiagnosticStatus, "/base/status", self._on_status, status_qos)
        self.create_subscription(DiagnosticStatus, "/base/aux_status", self._on_aux, status_qos)
        self.create_subscription(Odometry, "/odom/unfiltered", self._on_odom, 10)
        self.create_subscription(Imu, "/imu/data_raw", self._on_imu, qos_profile_sensor_data)

    def _on_status(self, m):
        now = time.monotonic()
        with self.lock:
            self.status = kv(m)
            self.status_rx = now
            self.status_count += 1
            self.status_times.append(now)

    def _on_aux(self, m):
        with self.lock:
            self.aux = kv(m)
            self.aux_times.append(time.monotonic())

    def _on_odom(self, m):
        with self.lock:
            self.odom = m
            self.odom_count += 1
            self.odom_max_v = max(self.odom_max_v, m.twist.twist.linear.x)

    def _on_imu(self, m):
        with self.lock:
            self.imu_count += 1

    def snapshot(self):
        with self.lock:
            return dict(self.status) if self.status else {}

    def cmd(self, v, w=0.0):
        t = Twist()
        t.linear.x = float(v)
        t.angular.z = float(w)
        self.cmd_pub.publish(t)


class Chain:
    """Owns every process and emulator of one scenario."""

    def __init__(self, tmp, override=True, commissioned=True, start_agent=True):
        self.tmp = tmp
        self.pty = os.path.join(tmp, "base_tty")
        self.ctl_path = os.path.join(tmp, "base.ctl")
        self.logs = {}
        self.can1 = can.Bus(interface="socketcan", channel=CAN1)
        self.can2 = can.Bus(interface="socketcan", channel=CAN2)
        self.zlac = Zlac8015dEmulator(self.can1).start()
        self.bms = DalyBmsEmulator(self.can2).start()
        args = [BASE_BIN, "--can1", CAN1, "--can2", CAN2, "--pty", self.pty, "--ctl", self.ctl_path]
        if commissioned:
            args += ["--commissioning", COMMISSIONING]
        if override:
            args.append("--sil-safety-override")
        self.base = self._spawn("base", args)
        self._wait_path(self.ctl_path, 5.0)
        self.agent = None
        if start_agent:
            self.start_agent()
        self.node = Observer()
        self._spin = True
        self._spinner = threading.Thread(target=self._spin_loop, daemon=True)
        self._spinner.start()

    # --- processes -----------------------------------------------------
    def _spawn(self, name, args, shell=False):
        log = open(os.path.join(self.tmp, name + ".log"), "w")
        self.logs[name] = log.name
        return subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, shell=shell,
                                start_new_session=True, executable="/bin/bash" if shell else None)

    @staticmethod
    def _wait_path(path, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if os.path.exists(path):
                return
            time.sleep(0.05)
        raise RuntimeError("timeout waiting for %s" % path)

    def start_agent(self):
        cmd = ("source /opt/ros/jazzy/setup.bash && source %s && exec ros2 run micro_ros_agent "
               "micro_ros_agent serial --dev %s -b 115200" % (AGENT_SETUP, self.pty))
        self.agent = self._spawn("agent", cmd, shell=True)

    def kill_agent(self):
        if self.agent is not None:
            os.killpg(self.agent.pid, signal.SIGKILL)
            self.agent.wait(timeout=5)
            self.agent = None

    def _spin_loop(self):
        while self._spin:
            rclpy.spin_once(self.node, timeout_sec=0.01)

    def close(self):
        self._spin = False
        self._spinner.join(timeout=2)
        self.node.destroy_node()
        self.kill_agent()
        if self.base.poll() is None:
            self.base.send_signal(signal.SIGTERM)
            try:
                self.base.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.base.kill()
        self.zlac.stop()
        self.bms.stop()
        for b in (self.can1, self.can2):
            try:
                b.shutdown()
            except Exception:
                pass

    # --- control socket ----------------------------------------------------
    def ctl(self, *cmds):
        out = []
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.settimeout(2.0)
            s.connect(self.ctl_path)
            for c in cmds:
                s.sendall((c + "\n").encode())
                out.append(s.recv(1024).decode().strip())
        return out

    def state(self):
        line = self.ctl("state")[0]
        return dict(p.split("=", 1) for p in line.split())

    # --- helpers -------------------------------------------------------------
    def wait_for(self, pred, timeout, period=0.02, what="condition"):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if pred():
                return True
            time.sleep(period)
        raise AssertionError("timeout (%.1f s) waiting for %s; last status %s" % (
            timeout, what, self.node.snapshot()))

    def wait_status(self, timeout=20.0):
        return self.wait_for(lambda: self.node.status is not None, timeout, what="/base/status")

    def arm(self):
        """PNOZ cycle as the operator does it: safety chain closed, RESET pressed
        and released, 2nd E-stop released, software stop cleared."""
        self.ctl("column released", "estop 0", "safety_ok 0")
        time.sleep(0.4)
        self.ctl("reset 1")
        time.sleep(0.25)
        self.ctl("reset 0", "safety_ok 1")
        time.sleep(0.1)
        self.ctl("sw_stop 0")
        self.wait_for(lambda: self.state().get("safety") == "RUN", 3.0, what="safety RUN")

    def drive(self, v, w, seconds, rate_hz=20.0):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.node.cmd(v, w)
            time.sleep(1.0 / rate_hz)

    def zlac_target_rpm(self):
        with self.zlac.lock:
            return [self.zlac.target_rpm(0), self.zlac.target_rpm(1)]

    def zlac_rpm(self):
        with self.zlac.lock:
            return list(self.zlac.rpm)


@pytest.fixture
def chain_factory(prerequisites, request):
    made = []
    tmp = tempfile.mkdtemp(prefix="oamr_sil_")

    def make(**kw):
        c = Chain(tmp, **kw)
        made.append(c)
        return c
    yield make
    for c in made:
        c.close()
    if SIL_LOG_DIR:
        dst = os.path.join(SIL_LOG_DIR, request.node.name)
        shutil.copytree(tmp, dst, dirs_exist_ok=True, ignore=shutil.ignore_patterns("*.ctl", "*tty"))
    shutil.rmtree(tmp, ignore_errors=True)
    for name in (CAN1, CAN2):
        vcan_up(name)  # a scenario may have deleted one


@pytest.fixture
def chain(chain_factory):
    c = chain_factory()
    c.wait_status()
    return c
