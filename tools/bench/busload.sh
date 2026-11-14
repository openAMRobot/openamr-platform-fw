#!/usr/bin/env bash
# busload.sh: measure CAN bus load with canbusload (can-utils) for a fixed time.
#
# Proves: the bus load of one CAN channel stays below the agreed limit while the nodes run
# their normal traffic, so heartbeats and the 10 Hz status frames are not starved.
# Run it per channel during the G4 bench, once idle and once with the emulated or real
# drive commanded at the capped bench speed.
#
# Pass: the highest load seen is at most MAX_LOAD_PERCENT (exit 0). The limit is a named
# parameter, TODO(Evans): set the agreed limit (default 50 is a placeholder, not a decision).
#
# Usage: busload.sh <iface> <bitrate> [seconds]
#   MAX_LOAD_PERCENT=50 busload.sh can0 500000 30
set -euo pipefail

[ $# -ge 2 ] || { echo "usage: $0 <iface> <bitrate> [seconds]" >&2; exit 2; }
iface=$1
bitrate=$2
seconds=${3:-30}
max_load=${MAX_LOAD_PERCENT:-50}

command -v canbusload >/dev/null || { echo "canbusload not found (apt install can-utils)" >&2; exit 2; }

out=$(timeout "$seconds" stdbuf -oL canbusload "$iface@$bitrate" 2>&1 || true)
echo "$out"
peak=$(grep -oE '[0-9]+%' <<<"$out" | tr -d '%' | sort -n | tail -1)
if [ -z "$peak" ]; then
    echo "FAIL: no load figure read from canbusload" >&2
    exit 1
fi
echo "peak load ${peak}% (limit ${max_load}%)"
if [ "$peak" -le "$max_load" ]; then
    echo "PASS"
else
    echo "FAIL: peak load above limit" >&2
    exit 1
fi
