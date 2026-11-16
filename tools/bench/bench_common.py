"""Shared helpers for the bench scripts: bus, SDO client, motion guard, 603Fh decoder.

The SDO client is the one used by the SIL emulator tests, so the scripts are exercised
against the emulator before they touch hardware.
"""
import math
import os
import sys

import can

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "can_sil"))
from canopen_common import SdoAbort, SdoClient  # noqa: E402,F401

MAX_SPEED_M_S = 0.1  # bench cap for every commanded wheel speed (P-03 bring-up limit for G4)

FAULT_NAMES = {
    0x0001: "over-voltage",
    0x0002: "under-voltage",
    0x0100: "EEPROM",
    0x0004: "over-current",
    0x0008: "overload",
    0x0020: "encoder out of tolerance",
    0x0080: "reference voltage",
    0x0200: "Hall",
    0x0400: "motor over-temperature",
    0x0800: "encoder error",
    0x2000: "speed setting error",
}
GLOBAL_MASK = 0x0003 | 0x0100


def open_bus(channel):
    return can.Bus(interface="socketcan", channel=channel)


def decode_603f(value):
    """Return a list of strings. Global bits sit in the low half, per-side bits are repeated
    in the high half (left) and low half (right)."""
    out = []
    for bit, name in FAULT_NAMES.items():
        if bit & GLOBAL_MASK and value & bit:
            out.append(name)
    for bit, name in FAULT_NAMES.items():
        if bit & GLOBAL_MASK:
            continue
        if value & bit:
            out.append("right: " + name)
        if value & (bit << 16):
            out.append("left: " + name)
    return out


def add_motion_args(ap):
    ap.add_argument("--wheels-up-confirmed", action="store_true",
                    help="required for any command that can turn a wheel: both drive wheels are "
                         "off the ground and nobody is near them")
    ap.add_argument("--wheel-diameter-m", type=float, default=None,
                    help="drive wheel diameter in metres (required for motion, no default: "
                         "TODO(Evans) measure on the built robot)")


def require_motion_allowed(args):
    if not args.wheels_up_confirmed:
        sys.exit("refusing to send motion commands: pass --wheels-up-confirmed after lifting "
                 "both drive wheels off the ground")
    if not args.wheel_diameter_m or args.wheel_diameter_m <= 0:
        sys.exit("refusing to send motion commands: --wheel-diameter-m is required")


def speed_to_rpm(speed_m_s, wheel_diameter_m):
    """Wheel speed in r/min for a linear speed, clamped to MAX_SPEED_M_S (sign kept)."""
    if abs(speed_m_s) > MAX_SPEED_M_S:
        print("warning: speed %.3f m/s capped to %.2f m/s" % (speed_m_s, MAX_SPEED_M_S),
              file=sys.stderr)
        speed_m_s = math.copysign(MAX_SPEED_M_S, speed_m_s)
    return int(round(speed_m_s / (math.pi * wheel_diameter_m) * 60.0))
