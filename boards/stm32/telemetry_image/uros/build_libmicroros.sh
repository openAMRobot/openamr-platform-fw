#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Builds libmicroros.a for the TelemetryUrosSerialSimBench preset from the repositories of
# micro_ros.pins, each checked out at its pinned commit (a mismatch fails). Called by
# CMakeLists.txt at configure time; nothing is vendored. Follows micro_ros_setup jazzy
# (scripts/build_firmware.sh, config/generate_lib/generic/build.sh): a host workspace with the
# ament build tools, then the cross-compiled client workspace, merged into one archive.
#
# Needs: git, cmake, a host C compiler, colcon, Python 3 with catkin_pkg, empy 3 and lark;
# arm-none-eabi-gcc on PATH. No ROS installation is used: ROS paths are removed from the environment.
#
#   build_libmicroros.sh OUT_DIR
# Leaves OUT_DIR/libmicroros.a, OUT_DIR/include and OUT_DIR/pins.stamp.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$(mkdir -p "$1" && cd "$1" && pwd)"
pins="$here/micro_ros.pins"
stamp_text="$(cat "$pins" "$here/colcon.meta" "$here/toolchain-libmicroros.cmake" "$0" | sha256sum | cut -d' ' -f1)"
if [[ -f "$out/libmicroros.a" && "$(cat "$out/pins.stamp" 2>/dev/null)" == "$stamp_text" ]]; then
  echo "libmicroros: up to date ($out)"
  exit 0
fi
rm -rf "$out/dev_ws" "$out/mcu_ws" "$out/include" "$out/libmicroros.a" "$out/pins.stamp"
mkdir -p "$out/dev_ws/src" "$out/mcu_ws/src"

while read -r ws name url commit _rest; do
  [[ -z "$ws" || "$ws" == \#* ]] && continue
  dir="$out/${ws}_ws/src/$name"
  git init -q "$dir"
  git -C "$dir" fetch -q --depth 1 "$url" "$commit"
  git -C "$dir" -c advice.detachedHead=false checkout -q FETCH_HEAD
  got="$(git -C "$dir" rev-parse HEAD)"
  [[ "$got" == "$commit" ]] || { echo "libmicroros: $name resolved to $got, pinned $commit" >&2; exit 1; }
done < "$pins"

# Packages micro_ros_setup ignores, plus the C++ and test parts this client does not use.
for p in ros2/rcl_logging/rcl_logging_spdlog ros2/ros2_tracing/test_tracetools ros2/ros2_tracing/lttngpy \
         ros2/rosidl/rosidl_typesupport_introspection_cpp uros/rcl/rcl_yaml_param_parser uros/rclc/rclc_examples \
         ros2/rcl_interfaces/test_msgs; do
  [[ -d "$out/mcu_ws/src/$p" ]] && touch "$out/mcu_ws/src/$p/COLCON_IGNORE"
done

# A clean environment: no ROS installation may leak into either workspace, and nothing of the
# calling CMake (this script runs from execute_process of the image's configure step, whose
# environment carries the cross compiler) may reach the host workspace.
unset AMENT_PREFIX_PATH CMAKE_PREFIX_PATH COLCON_PREFIX_PATH PYTHONPATH ROS_DISTRO ROS_VERSION LD_LIBRARY_PATH || true
unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS ASMFLAGS CMAKE_TOOLCHAIN_FILE CMAKE_GENERATOR CMAKE_BUILD_TYPE \
      CMAKE_EXPORT_COMPILE_COMMANDS MAKEFLAGS || true
export CC=cc CXX=c++   # host compilers for the ament build tools; the client workspace sets its own

(cd "$out/dev_ws" && colcon build --event-handlers console_cohesion- --packages-up-to ament_cmake_ros ament_cmake_gtest \
   --cmake-args -DBUILD_TESTING=OFF)
set +u; . "$out/dev_ws/install/local_setup.bash"; set -u
(cd "$out/mcu_ws" && colcon build --merge-install --event-handlers console_cohesion- \
   --packages-ignore-regex='.*_cpp' --metas "$here/colcon.meta" \
   --packages-up-to rclc micro_ros_utilities rmw_microxrcedds std_msgs sensor_msgs \
   --cmake-args --no-warn-unused-cli -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=OFF -DTHIRDPARTY=ON \
     -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release \
     -DCMAKE_TOOLCHAIN_FILE="$here/toolchain-libmicroros.cmake")

# One archive, member names prefixed by their library (as micro_ros_setup does).
tmp="$out/merge"; rm -rf "$tmp"; mkdir -p "$tmp"
while IFS= read -r lib; do
  base="$(basename "$lib" .a)"; mkdir -p "$tmp/$base"
  (cd "$tmp/$base" && arm-none-eabi-ar x "$lib" && for f in *; do mv "$f" "../$base-$f"; done)
  rmdir "$tmp/$base"
done < <(find "$out/mcu_ws/install/lib" -name '*.a' | sort)
(cd "$tmp" && arm-none-eabi-ar rc "$out/libmicroros.a" ./*.o* && arm-none-eabi-ranlib "$out/libmicroros.a")
rm -rf "$tmp"
mkdir -p "$out/include"
(cd "$out/mcu_ws/install/include" && find . -name '*.h' -exec cp --parents {} "$out/include/" \;)
echo "$stamp_text" > "$out/pins.stamp"
echo "libmicroros: built $out/libmicroros.a"
