#!/usr/bin/env bash
# Mutation check for the host tests: each mutation of a copy of the overlay must make the named
# tests fail. Proves the PID and config tests detect the behaviour they claim to test.
# Usage: tests/host/mutation_check.sh <linorobot2_hardware checkout> [work dir]
# The overlay in the repository is never modified.
set -euo pipefail
upstream="$1"
work="${2:-build/mutation}"
repo="$(cd "$(dirname "$0")/../.." && pwd)"
overlay="$repo/boards/teensy_4_0/linorobot2_overlay"
rm -rf "$work"; mkdir -p "$work"

# name | file in the overlay | perl substitution | test that must fail (ctest regex)
mutations=(
  "no-anti-windup|lib/pid/pid.cpp|s/^\s*if \(ki_ > 1e-6\) integral_ -= .*\n//mg|AntiWindup"
  "empty-reset|lib/pid/pid.cpp|s/(void PID::reset\(\)\s*\{)[^}]*\}/\$1}/s|ResetClearsIntegral"
  "no-zero-reset|lib/pid/pid.cpp|s/(if \(setpoint == 0 && error == 0\)\s*\{)[^}]*\}/\$1}/s|ZeroSetpointAndZeroErrorResetsIntegral"
  "no-update-kp|lib/pid/pid.cpp|s/kp_ = kp;/(void)kp;/|UpdateConstantsTakesEffect"
  "no-clamp|lib/pid/pid.cpp|s/^\s*pid = m.._val_;\n//mg|OutputStaysWithinMinMax"
)
failed=0
for m in "${mutations[@]}"; do
  IFS='|' read -r name file expr want <<<"$m"
  dir="$work/$name"
  cp -r "$overlay" "$dir"
  perl -0pi -e "$expr" "$dir/$file"
  if cmp -s "$overlay/$file" "$dir/$file"; then
    echo "MUTATION $name: substitution did not change $file"; failed=1; continue
  fi
  cmake -S "$repo/tests/host" -B "$dir/build" -G Ninja -DLINOROBOT2_HARDWARE_DIR="$upstream" \
        -DOVERLAY_DIR="$dir" >/dev/null
  if ! cmake --build "$dir/build" >"$dir/build.log" 2>&1; then
    echo "MUTATION $name: does not compile (see $dir/build.log); not a valid mutation"; failed=1; continue
  fi
  if ctest --test-dir "$dir/build" -R "$want" --no-tests=error >"$dir/ctest.log" 2>&1; then
    echo "MUTATION $name: SURVIVED (tests matching $want still pass)"; failed=1
  else
    echo "MUTATION $name: killed ($(grep -cE 'Failed|\*\*\*Failed' "$dir/ctest.log") failing test lines matching $want)"
  fi
done

# The PWM test is skipped (issue #25); without the skip it must fail on today's header.
dir="$work/pwm-unskipped"
mkdir -p "$dir"
cp -r "$repo/tests/host" "$dir/host"
sed -i '/GTEST_SKIP() << "PWM_MIN expands to -1025/d' "$dir/host/test_lino_base_config.cpp"
cmake -S "$dir/host" -B "$dir/build" -G Ninja -DLINOROBOT2_HARDWARE_DIR="$upstream" -DOVERLAY_DIR="$overlay" >/dev/null
cmake --build "$dir/build" >/dev/null
if ctest --test-dir "$dir/build" -R PwmMinIsMinusPwmMax --output-on-failure >"$dir/ctest.log" 2>&1; then
  echo "PWM test without skip: PASSED (issue #25 may be fixed; remove the skip)"
else
  echo "PWM test without skip: fails as expected:"; grep -A4 'Expected equality' "$dir/ctest.log" | head -5
fi
exit "$failed"
