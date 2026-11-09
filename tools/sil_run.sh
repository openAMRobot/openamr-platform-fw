#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Start the whole software-in-the-loop chain with one command:
#
#   ZLAC8015D emulator (vcan0) + Daly BMS emulator (vcan1)
#   + oamr_base_host (the firmware core, host build) + micro-ROS agent (pty)
#
#   tools/sil_run.sh [--no-override] [--no-commissioning]
#
# Then, in another shell with ROS 2 Jazzy sourced:
#   tools/sil_ctl.py arm                          # PNOZ cycle (SIL override only)
#   ros2 topic pub -r 20 /cmd_vel geometry_msgs/msg/Twist "{linear: {x: 0.2}}"
#   ros2 topic echo /base/status --field values
# Ctrl-C stops everything. Logs: $SIL_LOG_DIR (default /tmp/oamr_sil).
#
# --sil-safety-override (on by default here) lets the control socket drive the
# safety GPIO; it exists only in the host build. Without it the base stays
# LOCKED, as the STM32 does today (safety pins TODO(Evans)).
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
UROS_WS="${UROS_WS:-$HOME/uros}"
LOG="${SIL_LOG_DIR:-/tmp/oamr_sil}"
PTY="${SIL_PTY:-/tmp/oamr_base_tty}"
CTL="${SIL_CTL:-/tmp/oamr_base.ctl}"
BIN="$REPO/build/host/oamr_base_host"
OVERRIDE=--sil-safety-override
COMM=(--commissioning "$REPO/boards/host/config/sil_commissioning.conf")
for a in "$@"; do
    case "$a" in
        --no-override) OVERRIDE="" ;;
        --no-commissioning) COMM=() ;;
        *) echo "usage: $0 [--no-override] [--no-commissioning]"; exit 2 ;;
    esac
done
[ -x "$BIN" ] || { echo "build first: tools/sil_setup.sh"; exit 1; }
set +u
# shellcheck disable=SC1091
source /opt/ros/jazzy/setup.bash
# shellcheck disable=SC1091
source "$UROS_WS/install/local_setup.bash"
set -u
SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO="sudo -n"
for ifc in vcan0 vcan1; do
    ip link show "$ifc" >/dev/null 2>&1 || { $SUDO modprobe vcan || true; $SUDO ip link add dev "$ifc" type vcan; }
    $SUDO ip link set up "$ifc"
done
mkdir -p "$LOG"

PIDS=()
cleanup() {
    for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null || true; done
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

python3 "$REPO/tools/can_sil/zlac8015d_emulator.py" --channel vcan0 --print-interval 1.0 \
    < /dev/null > "$LOG/zlac.log" 2>&1 &
PIDS+=($!)
python3 "$REPO/tools/can_sil/daly_bms_emulator.py" --channel vcan1 < /dev/null > "$LOG/daly.log" 2>&1 &
PIDS+=($!)
# shellcheck disable=SC2086
"$BIN" --can1 vcan0 --can2 vcan1 --pty "$PTY" --ctl "$CTL" "${COMM[@]}" $OVERRIDE \
    > "$LOG/base.log" 2>&1 &
PIDS+=($!)
for _ in $(seq 50); do [ -e "$PTY" ] && break; sleep 0.1; done
ros2 run micro_ros_agent micro_ros_agent serial --dev "$PTY" -b 115200 > "$LOG/agent.log" 2>&1 &
PIDS+=($!)

echo "SIL chain running (logs in $LOG, control socket $CTL). Ctrl-C to stop."
head -1 "$LOG/base.log" 2>/dev/null || true
wait -n "${PIDS[@]}" || true
echo "a SIL process exited; see $LOG"
