#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build and run the host tests of the portable firmware core (tests/host/core).
# Needs: cmake >= 3.22, a C11/C++17 compiler, libgtest-dev (Ubuntu 24.04), python3.
# Usage: tools/host_tests.sh [build-dir]
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-${repo_root}/build/host-core}"

cmake -S "${repo_root}/tests/host/core" -B "${build_dir}" -DCMAKE_BUILD_TYPE=Debug
cmake --build "${build_dir}" --parallel
ctest --test-dir "${build_dir}" --output-on-failure --no-tests=error --timeout 120
