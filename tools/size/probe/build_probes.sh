#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Size probes for the runtime decision (docs/architecture/runtime-decision.md).
# NOT FIRMWARE: each image links real third-party code (micro-ROS, LwIP,
# FreeRTOS) with stub I/O against the board linker script, only to read flash
# and RAM per region. The images are never flashed.
#
# Inputs (environment, all required except OUT):
#   UROS_LIB   folder with libmicroros.a and microros_include/ produced by
#              micro_ros_stm32cubemx_utils (jazzy) with the board CPU flags
#   UROS_UTILS checkout of micro_ros_stm32cubemx_utils (for extra_sources/)
#   LWIP       checkout of lwip (src/ and contrib/ports/freertos)
#   FREERTOS   checkout of FreeRTOS-Kernel
#   OUT        output folder (default: build/size-probes)
# Output: one .elf and .map per probe and OUT/report.md from map_report.py.
# A probe that overflows a region is still linked (--noinhibit-exec) so that
# the map shows by how much; the overflow is listed in report.md.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
BOARD="$REPO/boards/stm32/OpenAMRobot"
OUT="${OUT:-$REPO/build/size-probes}"
: "${UROS_LIB:?set UROS_LIB}" "${UROS_UTILS:?set UROS_UTILS}" "${LWIP:?set LWIP}" "${FREERTOS:?set FREERTOS}"
mkdir -p "$OUT"

CC=arm-none-eabi-gcc
# Same CPU and optimisation flags as the board Release preset.
CPU=(-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb)
CFLAGS=("${CPU[@]}" -Os -g0 -std=gnu11 -ffunction-sections -fdata-sections -w)
LDFLAGS=("${CPU[@]}" --specs=nano.specs -T "$BOARD/STM32H723xG_flash.ld" -Wl,--gc-sections -Wl,--print-memory-usage -Wl,--noinhibit-exec)

LWIP_CORE=(
  "$LWIP"/src/core/{init,def,inet_chksum,ip,mem,memp,netif,pbuf,timeouts,udp,raw}.c
  "$LWIP"/src/core/ipv4/{etharp,icmp,ip4,ip4_addr,ip4_frag}.c
  "$LWIP"/src/netif/ethernet.c
)
LWIP_API=("$LWIP"/src/api/{api_lib,api_msg,err,netbuf,netifapi,sockets,tcpip}.c "$LWIP"/contrib/ports/freertos/sys_arch.c)
RTOS=("$FREERTOS"/{tasks,queue,list,timers,event_groups,stream_buffer}.c "$FREERTOS"/portable/GCC/ARM_CM7/r0p1/port.c "$FREERTOS"/portable/MemMang/heap_4.c)
UROS_INC=(-I"$UROS_LIB/microros_include")

# Objects get readable names so that map_report groups by source file.
OBJ=()
compile() { # probe, extra flags (array name), sources...
  local probe="$1" flags="$2"; shift 2
  local -n f="$flags"
  mkdir -p "$OUT/obj/$probe"
  for src in "$@"; do
    local o="$OUT/obj/$probe/$(basename "${src%.*}").o"
    case "$src" in
      *.s) "$CC" "${CPU[@]}" -x assembler-with-cpp -c "$src" -o "$o" ;;
      *)   "$CC" "${CFLAGS[@]}" "${f[@]}" -c "$src" -o "$o" ;;
    esac
    OBJ+=("$o")
  done
}

link() { # probe; uses OBJ, then clears it
  local name="$1" rc=0
  timeout 900 "$CC" "${LDFLAGS[@]}" -Wl,-Map="$OUT/$name.map" -o "$OUT/$name.elf" \
    "${OBJ[@]}" "$UROS_LIB/libmicroros.a" -lc_nano -lm > "$OUT/$name.ld.txt" 2>&1 || rc=$?
  OBJ=()
  if [ "$rc" -ne 0 ]; then grep -E "error|undefined" "$OUT/$name.ld.txt" | head -40; return "$rc"; fi
}

# 0. Empty baseline: startup and libc stubs only. Deltas are taken over this.
F0=(-I"$HERE/common")
compile empty_baseline F0 "$BOARD/startup_stm32h723xx.s" "$HERE/common/probe_stubs.c" "$HERE/common/probe_main_empty.c"
link empty_baseline

# 1. Super-loop + micro-ROS over a byte stream (serial bench transport).
F1=(-DPROBE_TRANSPORT_SERIAL -DPROBE_HEAP_BYTES=32768 -I"$HERE/common" "${UROS_INC[@]}")
compile superloop_uros_serial F1 "$BOARD/startup_stm32h723xx.s" "$HERE/common/probe_stubs.c" \
  "$HERE/uros/probe_main_superloop.c" "$HERE/uros/probe_uros_app.c" "$HERE/uros/probe_transport_serial.c"
link superloop_uros_serial

# 2. Super-loop + LwIP raw API (NO_SYS=1) + micro-ROS over UDP.
F2=(-DPROBE_WITH_LWIP -DPROBE_HEAP_BYTES=32768 -I"$HERE/lwip_nosys" -I"$HERE/common" -I"$LWIP/src/include" "${UROS_INC[@]}")
compile superloop_lwip_uros_udp F2 "$BOARD/startup_stm32h723xx.s" "$HERE/common/probe_stubs.c" \
  "$HERE/uros/probe_main_superloop.c" "$HERE/uros/probe_uros_app.c" "$HERE/lwip_nosys/probe_net_nosys.c" \
  "${LWIP_CORE[@]}"
link superloop_lwip_uros_udp

# 3. FreeRTOS + LwIP sockets + micro-ROS over UDP (upstream pattern).
F3=(-DPROBE_UPSTREAM_TIME -DPROBE_HEAP_BYTES=1024 -I"$HERE/lwip_rtos" -I"$HERE/common" -I"$LWIP/src/include"
    -I"$LWIP/contrib/ports/freertos/include" -I"$FREERTOS/include" -I"$FREERTOS/portable/GCC/ARM_CM7/r0p1"
    "${UROS_INC[@]}")
F3H=("${F3[@]}" -DPROBE_UROS_HEAP_BYTES=32768)
compile freertos_lwip_uros_udp F3 "$BOARD/startup_stm32h723xx.s" "$HERE/common/probe_stubs.c" \
  "$HERE/uros/probe_uros_app.c" "$HERE/lwip_rtos/probe_net_rtos.c" \
  "$UROS_UTILS/extra_sources/microros_allocators.c" "$UROS_UTILS/extra_sources/microros_time.c" \
  "${LWIP_CORE[@]}" "${LWIP_API[@]}" "${RTOS[@]}"
compile freertos_lwip_uros_udp F3H "$UROS_UTILS/extra_sources/custom_memory_manager.c"
link freertos_lwip_uros_udp

{
  for p in empty_baseline superloop_uros_serial superloop_lwip_uros_udp freertos_lwip_uros_udp; do
    python3 "$REPO/tools/size/map_report.py" --top 8 --title "$p" "$OUT/$p.map"
    grep -h "overflowed by\|will not fit" "$OUT/$p.ld.txt" | sed 's/^.*ld: /- ld: /' || true
    echo
  done
} > "$OUT/report.md"
echo "wrote $OUT/report.md"
