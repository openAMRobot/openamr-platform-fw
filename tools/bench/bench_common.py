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

MAX_SPEED_M_S = 0.1  # bench cap for every commanded wheel speed (G4 bring-up, wheels up)
REFUSE_ABOVE_M_S = 0.2  # above this a request is refused, not reduced (typo guard)
CEILING_M_S = 0.5  # P-03 item 30 A5 speed ceiling: never commanded by a bench script

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

BENCH_NOTICE = ("BENCH ONLY: this script can command a drive. Run it only under the owner's "
                "bench procedure, with a second person at the hardwired E-stop. It is not a "
                "safety function and its output is not stop evidence.")

SPEED_RESOLUTION_OBJECT = (0x2026, 5)  # D-24: must read 1 before any speed maths
TOLERANCE = 0.10  # pass window for a measured speed against the commanded one


def open_bus(channel):
    return can.Bus(interface="socketcan", channel=channel)


def decode_603f(value):
    """Return a list of strings for the bits set in 603Fh. Both 16-bit halves use the same bit list.

    The vendor Quick Start contradicts itself on what the low and the high 16 bits mean, so the
    result names the bit range only ("603Fh[15:0]", "603Fh[31:16]") and never a wheel or side.
    TODO(Evans): settle the meaning on the real drive before anyone relies on it.
    """
    out = []
    for bit, name in FAULT_NAMES.items():
        if value & bit:
            out.append("603Fh[15:0]: " + name)
    for bit, name in FAULT_NAMES.items():
        if value & (bit << 16):
            out.append("603Fh[31:16]: " + name)
    return out


def add_bench_args(ap):
    ap.add_argument("--i-am-at-the-bench", action="store_true",
                    help="required: you are running the owner's bench procedure with a second "
                         "person at the E-stop")


def require_bench(args):
    """Print the bench notice and refuse (exit 2, nothing sent) without --i-am-at-the-bench."""
    print(BENCH_NOTICE, file=sys.stderr)
    if not args.i_am_at_the_bench:
        refuse("refusing to run: pass --i-am-at-the-bench once the owner's bench procedure is "
               "in hand and a second person stands at the E-stop")


def add_motion_args(ap):
    add_bench_args(ap)
    ap.add_argument("--wheels-up-confirmed", action="store_true",
                    help="required for any command that can turn a wheel: both drive wheels are "
                         "off the ground and nobody is near them")
    ap.add_argument("--wheel-diameter-m", type=float, default=None,
                    help="drive wheel diameter in metres (required for motion, no default: "
                         "TODO(Evans) measure on the built robot)")
    ap.add_argument("--allow-high-speed", action="store_true",
                    help="allow a speed above 0.2 m/s (up to the 0.5 m/s ceiling) and lift the "
                         "0.1 m/s bench cap; without it a request above 0.2 m/s is refused")


def refuse(message):
    """Print the reason on stderr and exit 2 (refused, nothing was sent)."""
    print(message, file=sys.stderr)
    sys.exit(2)


def require_motion_allowed(args):
    require_bench(args)
    if not args.wheels_up_confirmed:
        refuse("refusing to send motion commands: pass --wheels-up-confirmed after lifting "
                 "both drive wheels off the ground")
    if not args.wheel_diameter_m or args.wheel_diameter_m <= 0:
        refuse("refusing to send motion commands: --wheel-diameter-m is required")


def speed_to_rpm(speed_m_s, wheel_diameter_m, allow_high_speed=False):
    """Wheel speed in r/min for a linear speed (sign kept).

    Up to MAX_SPEED_M_S the request is used as given. Between MAX_SPEED_M_S and
    REFUSE_ABOVE_M_S it is capped to MAX_SPEED_M_S with a warning. Above REFUSE_ABOVE_M_S it is
    refused (exit 2) unless allow_high_speed is set, so a typo cannot spin the wheels at the
    0.5 m/s ceiling. With allow_high_speed the cap is lifted up to CEILING_M_S; anything above
    the ceiling is always refused.
    """
    mag = abs(speed_m_s)
    if mag > CEILING_M_S:
        refuse("refusing: %.3f m/s is above the %.1f m/s speed ceiling" % (mag, CEILING_M_S))
    if mag > REFUSE_ABOVE_M_S and not allow_high_speed:
        refuse("refusing: %.3f m/s is above %.1f m/s; pass --allow-high-speed to confirm "
                 "this is intended (wheels up, E-stop in reach)" % (mag, REFUSE_ABOVE_M_S))
    if mag > MAX_SPEED_M_S and not allow_high_speed:
        print("warning: speed %.3f m/s capped to %.2f m/s" % (mag, MAX_SPEED_M_S),
              file=sys.stderr)
        speed_m_s = math.copysign(MAX_SPEED_M_S, speed_m_s)
    return int(round(speed_m_s / (math.pi * wheel_diameter_m) * 60.0))


def require_speed_resolution(sdo):
    """D-24: read 2026h:05 and require 1 before any speed maths. Returns on success, refuses
    (exit 2) when the object cannot be read or does not read 1."""
    index, sub = SPEED_RESOLUTION_OBJECT
    try:
        value = sdo.read(index, sub)
    except (TimeoutError, SdoAbort) as exc:
        refuse("refusing: cannot read 2026h:05 (speed resolution): %s" % exc)
    if value != 1:
        refuse("refusing: 2026h:05 (speed resolution) reads %d, 1 is required before any speed "
               "maths (D-24)" % value)
    return value


def read_wheel_speeds(sdo):
    """Read 606Ch:01 and 606Ch:02 (0.1 r/min). Raises TimeoutError or SdoAbort when either read
    fails: there is no partial result."""
    return (sdo.read(0x606C, 1, signed=True) / 10.0, sdo.read(0x606C, 2, signed=True) / 10.0)


def speed_within(measured, commanded, tolerance=TOLERANCE):
    """True when measured has the sign of commanded and is within tolerance of it."""
    if commanded == 0 or measured is None:
        return False
    if (measured > 0) != (commanded > 0):
        return False
    return abs(measured - commanded) <= abs(commanded) * tolerance
