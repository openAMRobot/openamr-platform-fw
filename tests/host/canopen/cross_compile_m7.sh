#!/usr/bin/env bash
# Cross-compile firmware/canopen and firmware/drive_telemetry for the base
# controller core (Cortex-M7, hard float, as the STM32H723 board project) as
# objects only, with the host warning set, and print section sizes.
# Usage: tests/host/canopen/cross_compile_m7.sh [out_dir]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
OUT="${1:-${ROOT}/build/canopen-m7}"
CC="${CC_ARM:-arm-none-eabi-gcc}"
mkdir -p "${OUT}"
FLAGS=(-std=c11 -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
       -Os -ffunction-sections -fdata-sections
       -Wall -Wextra -Werror -Wconversion -Wshadow -Wstrict-prototypes -Wpedantic
       -I"${ROOT}/firmware/canopen/include" -I"${ROOT}/firmware/drive_telemetry/include")
objs=()
for src in "${ROOT}"/firmware/canopen/src/*.c "${ROOT}"/firmware/drive_telemetry/src/*.c; do
  o="${OUT}/$(basename "${src%.c}").o"
  "${CC}" "${FLAGS[@]}" -c "${src}" -o "${o}"
  objs+=("${o}")
done
"${CC}" --version | head -1
arm-none-eabi-size -t "${objs[@]}"
