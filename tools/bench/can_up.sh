#!/usr/bin/env bash
# can_up.sh: bring up a SocketCAN interface for bench work.
#
# Proves: the interface runs at the bitrate the node expects, with automatic bus-off restart
# disabled while the bus is being brought up (a node that cannot join must stay visible as
# bus-off instead of restarting silently), and with a transmit queue long enough for bursts.
# Bitrates: CAN1 ZLAC8015D 500000, CAN2 Daly BMS 250000, CAN3 CTL-017 500000.
#
# Pass: the final `ip -details link show` prints "state UP", the requested "bitrate N",
# "restart-ms 0" and "qlen 1000" (exit 0). Any other result exits non-zero.
#
# Usage: can_up.sh <iface> <bitrate> [--restart-ms N] [--dry-run]
#   --restart-ms N  set automatic bus-off restart after bring-up has been verified
#   --dry-run       print the commands without running them
set -euo pipefail

usage() {
    echo "usage: $0 <iface> <bitrate> [--restart-ms N] [--dry-run]" >&2
    exit 2
}

[ $# -ge 2 ] || usage
iface=$1
bitrate=$2
shift 2
restart_ms=0
dry_run=0
txqueuelen=1000
while [ $# -gt 0 ]; do
    case $1 in
        --restart-ms) restart_ms=${2:?missing value}; shift 2 ;;
        --dry-run) dry_run=1; shift ;;
        *) usage ;;
    esac
done
case $bitrate in
    125000 | 250000 | 500000 | 1000000) ;;
    *) echo "unsupported bitrate $bitrate (125000, 250000, 500000, 1000000)" >&2; exit 2 ;;
esac
case $restart_ms in
    '' | *[!0-9]*) echo "--restart-ms must be a number" >&2; exit 2 ;;
esac

run() {
    echo "+ $*"
    if [ "$dry_run" -eq 0 ]; then
        "$@"
    fi
}

sudo_cmd=()
if [ "$(id -u)" -ne 0 ] && [ "$dry_run" -eq 0 ]; then
    sudo_cmd=(sudo)
fi

run "${sudo_cmd[@]}" ip link set "$iface" down
run "${sudo_cmd[@]}" ip link set "$iface" type can bitrate "$bitrate" restart-ms 0
run "${sudo_cmd[@]}" ip link set "$iface" txqueuelen "$txqueuelen"
run "${sudo_cmd[@]}" ip link set "$iface" up

if [ "$dry_run" -eq 1 ]; then
    if [ "$restart_ms" -ne 0 ]; then
        echo "+ ip link set $iface type can restart-ms $restart_ms (after verification)"
    fi
    exit 0
fi

details=$(ip -details link show "$iface")
echo "$details"
fail=0
for want in "state UP" "bitrate $bitrate" "restart-ms 0" "qlen $txqueuelen"; do
    if ! grep -q "$want" <<<"$details"; then
        echo "FAIL: missing '$want'" >&2
        fail=1
    fi
done
if [ "$fail" -ne 0 ]; then
    exit 1
fi
if [ "$restart_ms" -ne 0 ]; then
    run "${sudo_cmd[@]}" ip link set "$iface" down
    run "${sudo_cmd[@]}" ip link set "$iface" type can bitrate "$bitrate" restart-ms "$restart_ms"
    run "${sudo_cmd[@]}" ip link set "$iface" up
fi
echo "PASS: $iface up at $bitrate bit/s, restart-ms $restart_ms"
