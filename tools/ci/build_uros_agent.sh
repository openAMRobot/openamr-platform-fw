#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Builds the micro-ROS agent for tools/ci/renode_uros.py, against an installed ROS 2 Jazzy, from
# pinned commits (micro_ros_msgs and micro-ROS-Agent, jazzy branches as of 15 Nov 2026). The
# agent package's own superbuild fetches eProsima Micro-XRCE-DDS-Agent at its tag v2.4.3
# (micro-ROS-Agent micro_ros_agent/cmake/SuperBuild.cmake) and uses the system Fast DDS.
# Test tooling only: nothing of it goes into the firmware image.
#
#   source /opt/ros/jazzy/setup.bash && tools/ci/build_uros_agent.sh WORKSPACE
# Leaves WORKSPACE/install; the executable is install/micro_ros_agent/lib/micro_ros_agent/micro_ros_agent.
set -euo pipefail
ws="$(mkdir -p "$1/src" && cd "$1" && pwd)"
pins=(
  "micro_ros_msgs https://github.com/micro-ROS/micro_ros_msgs.git b1ef85201e5545d3e07b73f4628bb15836062573"
  "micro-ROS-Agent https://github.com/micro-ROS/micro-ROS-Agent.git 7c932329ad5591ef23942ef1962534258c16b000"
)
for p in "${pins[@]}"; do
  read -r name url commit <<< "$p"
  dir="$ws/src/$name"
  rm -rf "$dir"; git init -q "$dir"
  git -C "$dir" fetch -q --depth 1 "$url" "$commit"
  git -C "$dir" -c advice.detachedHead=false checkout -q FETCH_HEAD
  [[ "$(git -C "$dir" rev-parse HEAD)" == "$commit" ]] || { echo "$name: not at $commit" >&2; exit 1; }
done
(cd "$ws" && colcon build --event-handlers console_cohesion- --cmake-args -DBUILD_TESTING=OFF -DUAGENT_USE_SYSTEM_LOGGER=OFF)
