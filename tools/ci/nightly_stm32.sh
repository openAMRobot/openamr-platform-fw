#!/usr/bin/env bash
# STM32 part of the "Firmware nightly" workflow, one CMake preset per call.
# Usage: tools/ci/nightly_stm32.sh PRESET FW_DIR HW_DIR OUT_DIR
# For every boards/stm32/<project>/ with a .ioc: build PRESET, check the Ethernet DMA descriptors
# are in RAM_D2, and (Debug only) check the .ioc against openamr-platform-hw's contract and the
# vendored ST files against their upstream release. Copies ELF, map and HEX with SHA-256 to
# OUT_DIR/firmware/ and appends check lines to OUT_DIR/checks.tsv. A tool that is not on the
# commit under test yet gives NOT RUN, never PASS. Exit 1 if any check failed.
set -uo pipefail
preset="$1"; fw="$(cd "$2" && pwd)"; hw="$(cd "$3" && pwd)"; out="$4"
mkdir -p "$out/logs" "$out/firmware"; out="$(cd "$out" && pwd)"
touch "$out/checks.tsv"
failed=0
p=$(echo "$preset" | tr '[:upper:]' '[:lower:]')

record() { printf '%s\t%s\t%s\n' "$1" "$2" "$3" >> "$out/checks.tsv"; echo "$2 $1${3:+: $3}"; }

shopt -s nullglob
projects=("$fw"/boards/stm32/*/*.ioc)
if (( ${#projects[@]} == 0 )); then
  record "stm32-build-$p" "NOT RUN" "no STM32 project on this commit"
  exit 0
fi
for ioc in "${projects[@]}"; do
  dir="$(dirname "$ioc")"; name="$(basename "$dir")"
  if (cd "$dir" && cmake --preset "$preset" && cmake --build --preset "$preset") > "$out/logs/stm32-build-$p-$name.log" 2>&1; then
    record "stm32-build-$p" PASS "$name $preset: $(arm-none-eabi-gcc --version | head -1)"
  else
    record "stm32-build-$p" FAIL "$name $preset (log logs/stm32-build-$p-$name.log)"; failed=1; continue
  fi
  elf="$dir/build/$preset/$name.elf"
  if python3 "$fw/tools/ci/check_dma_placement.py" "$elf" > "$out/logs/ram-d2-$p-$name.log" 2>&1; then
    record "ram-d2-$p" PASS "$name: Ethernet DMA descriptors in RAM_D2"
  else
    record "ram-d2-$p" FAIL "$name: $(grep -m1 'NOT in RAM_D2' "$out/logs/ram-d2-$p-$name.log")"; failed=1
  fi
  arm-none-eabi-objcopy -O ihex "$elf" "$out/firmware/$name-$p.hex"
  cp "$elf" "$out/firmware/$name-$p.elf"
  cp "$dir/build/$preset/$name.map" "$out/firmware/$name-$p.map" 2>/dev/null || true
  if [ "$preset" = Debug ]; then
    contract="$hw/interfaces/electrical/base-controller-io.yaml"
    if [ ! -f "$fw/tools/ci/check_ioc_contract.py" ]; then
      record ioc-contract "NOT RUN" "tools/ci/check_ioc_contract.py is not on this commit"
    elif [ ! -f "$contract" ]; then
      record ioc-contract "NOT RUN" "no base-controller-io.yaml on openamr-platform-hw $(git -C "$hw" rev-parse HEAD)"
    elif python "$fw/tools/ci/check_ioc_contract.py" "$ioc" --contract "$contract" \
           --contract-sha "$(git -C "$hw" rev-parse HEAD)" > "$out/logs/ioc-contract-$name.log" 2>&1; then
      record ioc-contract PASS "$name against openamr-platform-hw $(git -C "$hw" rev-parse --short HEAD)"
    else
      record ioc-contract FAIL "$name: $(grep -c '::error' "$out/logs/ioc-contract-$name.log") errors (log logs/ioc-contract-$name.log)"; failed=1
    fi
    if [ ! -f "$fw/tools/ci/check_vendor_provenance.py" ]; then
      record vendor-provenance "NOT RUN" "tools/ci/check_vendor_provenance.py is not on this commit"
    elif (cd "$fw" && python3 tools/ci/check_vendor_provenance.py "${dir#"$fw"/}" --cache "$out/../vendor-cache") \
           > "$out/logs/vendor-provenance-$name.log" 2>&1; then
      record vendor-provenance PASS "$name"
    else
      record vendor-provenance FAIL "$name: $(grep -c '::error' "$out/logs/vendor-provenance-$name.log") files (log logs/vendor-provenance-$name.log)"; failed=1
    fi
  fi
done
(cd "$out/firmware" && sha256sum -- * > SHA256SUMS-stm32-$p 2>/dev/null) || true
exit "$failed"
