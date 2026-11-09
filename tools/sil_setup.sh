#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# One-time setup of the software-in-the-loop chain on Ubuntu 24.04 + ROS 2 Jazzy.
#
#   tools/sil_setup.sh [UROS_WS]        (default UROS_WS=$HOME/uros)
#
# 1. micro_ros_setup (jazzy) in UROS_WS
# 2. micro-ROS "host" client libraries with RMW_UXRCE_TRANSPORT=custom (the
#    firmware owns the byte stream: pty on the host, UART/USB CDC on the MCU)
# 3. the micro-ROS agent
# 4. oamr_base_host in build/host
# 5. vcan0 / vcan1 (needs root or passwordless sudo)
# Takes about 15 minutes the first time; later runs only rebuild build/host.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
UROS_WS="${1:-$HOME/uros}"
set +u
# shellcheck disable=SC1091
source /opt/ros/jazzy/setup.bash
set -u
SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO="sudo -n"

mkdir -p "$UROS_WS/src"
cd "$UROS_WS"
if [ ! -d src/micro_ros_setup ]; then
    git clone -q --depth 1 -b jazzy https://github.com/micro-ROS/micro_ros_setup src/micro_ros_setup
fi
if [ ! -f install/micro_ros_setup/share/micro_ros_setup/package.xml ]; then
    rosdep update >/dev/null 2>&1 || true
    colcon build --packages-select micro_ros_setup
fi
set +u
# shellcheck disable=SC1091
source install/local_setup.bash
set -u

if [ ! -f install/rclc/lib/librclc.so ]; then
    [ -d firmware ] || ros2 run micro_ros_setup create_firmware_ws.sh host
    # Custom transport instead of the default UDP one; demos not needed.
    python3 - "$UROS_WS/src/colcon.meta" <<'EOF'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
a = d["names"]["rmw_microxrcedds"]["cmake-args"]
a = [x for x in a if not x.startswith("-DRMW_UXRCE_TRANSPORT") and "UDP" not in x]
a.append("-DRMW_UXRCE_TRANSPORT=custom")
d["names"]["rmw_microxrcedds"]["cmake-args"] = a
json.dump(d, open(p, "w"), indent=2)
EOF
    touch src/uros/micro-ROS-demos/COLCON_IGNORE 2>/dev/null || true
    ros2 run micro_ros_setup build_firmware.sh
fi
if [ ! -x install/micro_ros_agent/lib/micro_ros_agent/micro_ros_agent ]; then
    ros2 run micro_ros_setup create_agent_ws.sh
    ros2 run micro_ros_setup build_agent.sh
fi

cd "$REPO"
cmake -S boards/host -B build/host -DMICRO_ROS_PREFIX="$UROS_WS/install" >/dev/null
cmake --build build/host -j"$(nproc)"

for ifc in vcan0 vcan1; do
    if ! ip link show "$ifc" >/dev/null 2>&1; then
        $SUDO modprobe vcan || true
        $SUDO ip link add dev "$ifc" type vcan
    fi
    $SUDO ip link set up "$ifc"
done
python3 -c 'import can' 2>/dev/null || echo "note: python-can is required (pip install python-can)"
echo "SIL ready: build/host/oamr_base_host, agent in $UROS_WS/install"
