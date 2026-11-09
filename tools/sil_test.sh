#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Run the end-to-end SIL suite (tests/sil). Needs tools/sil_setup.sh first.
#   tools/sil_test.sh [pytest args]
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
UROS_WS="${UROS_WS:-$HOME/uros}"
set +u
# shellcheck disable=SC1091
source /opt/ros/jazzy/setup.bash
set -u
export OAMR_BASE_HOST="${OAMR_BASE_HOST:-$REPO/build/host/oamr_base_host}"
export MICRO_ROS_AGENT_SETUP="${MICRO_ROS_AGENT_SETUP:-$UROS_WS/install/local_setup.bash}"
cd "$REPO"
exec python3 -m pytest -q -p no:cacheprovider tests/sil "$@"
