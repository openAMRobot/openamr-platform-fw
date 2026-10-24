#!/usr/bin/env bash
# Host part of the "Firmware nightly" workflow: runs on the checkout of openamr-platform-fw main.
# Usage: tools/ci/nightly_fw.sh FW_DIR UPSTREAM_DIR OUT_DIR
#   FW_DIR        checkout of openamr-platform-fw (the commit under test)
#   UPSTREAM_DIR  linorobot2_hardware at the pinned base (for the C/C++ PID comparison)
# Appends "<name>\t<PASS|FAIL|NOT RUN>\t<detail>" lines to OUT_DIR/checks.tsv and writes logs to
# OUT_DIR/logs/. A check whose tool is not on the commit under test yet is NOT RUN with that
# reason, never PASS. Every check runs even after a failure. Exit 1 if any check failed.
set -uo pipefail
fw="$(cd "$1" && pwd)"; upstream="$(cd "$2" && pwd)"; out="$3"
mkdir -p "$out/logs"; out="$(cd "$out" && pwd)"
touch "$out/checks.tsv"
failed=0

record() { printf '%s\t%s\t%s\n' "$1" "$2" "$3" >> "$out/checks.tsv"; echo "$2 $1${3:+: $3}"; }

run() {  # run NAME DETAIL CMD...
  local name="$1" detail="$2"; shift 2
  if (cd "$fw" && "$@") > "$out/logs/$name.log" 2>&1; then
    record "$name" PASS "$detail"
  else
    record "$name" FAIL "$detail (log logs/$name.log)"; failed=1
  fi
}

ran_tests() { grep -qE '^Ran [1-9][0-9]* tests?' "$1"; }

# Python host tests (#23)
if ls "$fw"/tests/test_*.py >/dev/null 2>&1; then
  if (cd "$fw" && python -m unittest discover -s tests -v) > "$out/logs/python-host-tests.log" 2>&1 \
     && ran_tests "$out/logs/python-host-tests.log"; then
    record python-host-tests PASS "$(grep -E '^Ran ' "$out/logs/python-host-tests.log" | tail -1)"
  else
    record python-host-tests FAIL "see logs/python-host-tests.log"; failed=1
  fi
else
  record python-host-tests "NOT RUN" "no tests/test_*.py on this commit"
fi

# C/C++ host tests (GoogleTest, ASan + UBSan) and the mutation check
if [ -f "$fw/tests/host/CMakeLists.txt" ]; then
  if (cd "$fw" && cmake -S tests/host -B build/host -G Ninja -DLINOROBOT2_HARDWARE_DIR="$upstream" \
        && cmake --build build/host && ctest --test-dir build/host --output-on-failure --no-tests=error) \
        > "$out/logs/cpp-host-tests.log" 2>&1; then
    record cpp-host-tests PASS "$(grep -E 'tests passed' "$out/logs/cpp-host-tests.log" | tail -1); $(grep -c 'Skipped' "$out/logs/cpp-host-tests.log" || true) skipped with a tracking issue"
  else
    record cpp-host-tests FAIL "see logs/cpp-host-tests.log"; failed=1
  fi
  run mutation-check "each overlay PID mutation fails a test" bash tests/host/mutation_check.sh "$upstream" build/mutation
else
  record cpp-host-tests "NOT RUN" "tests/host is not on this commit"
  record mutation-check "NOT RUN" "tests/host is not on this commit"
fi

if [ -f "$fw/tools/ci/check_skips.py" ]; then
  run skip-tracking "every skip names a tracking issue" python tools/ci/check_skips.py tests
else
  record skip-tracking "NOT RUN" "tools/ci/check_skips.py is not on this commit"
fi

if [ -f "$fw/tools/ci/run_cppcheck.py" ]; then
  run cppcheck "$(cppcheck --version)" python tools/ci/run_cppcheck.py --root . --work build/cppcheck
else
  record cppcheck "NOT RUN" "tools/ci/run_cppcheck.py is not on this commit"
fi

if [ -f "$fw/tools/ci/check_format.sh" ]; then
  if (cd "$fw" && bash tools/ci/check_format.sh firmware) > "$out/logs/clang-format.log" 2>&1; then
    if grep -q 'NOT RUN' "$out/logs/clang-format.log"; then
      record clang-format "NOT RUN" "no C/C++ files under firmware/"
    else
      record clang-format PASS "firmware/"
    fi
  else
    record clang-format FAIL "see logs/clang-format.log"; failed=1
  fi
else
  record clang-format "NOT RUN" "tools/ci/check_format.sh is not on this commit"
fi
exit "$failed"
