#!/usr/bin/env python3
"""Record and replay CAN traces in candump -L format, so bench captures become regression inputs.

File format: one frame per line, exactly what `candump -L` writes and python-can's log reader
accepts: `(1731600000.123456) can1 601#2B00200000000000`. Extended IDs have 8 hex digits.
Remote frames are `123#R`. Lines starting with `#` and empty lines are ignored; any other line
that does not parse is an error (counted, listed, and fatal unless --lenient is given).

    can_trace.py record  CHANNEL OUT.log [--seconds N]      listen only, writes candump -L lines
    can_trace.py replay  IN.log CHANNEL [--ids 0x601,0x000] [--speed 1.0]
    can_trace.py check   IN.log CHANNEL --node zlac|ctl017 [--node-id N] [--exact]
                         [--volatile 606C,6041] [--speed S]

`check` is the regression run: the SDO requests and host frames of a trace are replayed into an
emulator that is listening on CHANNEL (start `zlac8015d_emulator.py` or `ctl017_emulator.py`
first, or use the library function check_trace() as the pytest suite does), and every SDO
response of the emulator is compared with the response in the trace. By default a response has
to be the same kind (acknowledge, upload of the same size, or abort with the same code) for the
same object; --exact also compares the data bytes, except for the objects in --volatile.

Safety: `replay` and `check` transmit frames. On a virtual interface (name starts with `vcan`)
they run freely. On any other interface they could command a real drive, so they print that
they need the owner's bench procedure and a second person at the hardwired E-stop, and refuse
(exit 2, nothing sent) without --i-am-at-the-bench. `record` never transmits.

A replay is protocol evidence only. A recorded trace shows what was on the wire, never that a
wheel or the lift stopped.
"""
import argparse
import re
import sys
import time

import can

LINE = re.compile(r"^\((\d+(?:\.\d+)?)\)\s+(\S+)\s+([0-9A-Fa-f]+)#(R|[0-9A-Fa-f]*)\s*$")
BENCH_NOTICE = ("BENCH ONLY: this transmits recorded frames on a real CAN interface and can "
                "command a drive. Run it only under the owner's bench procedure, with a second "
                "person at the hardwired E-stop. It is not a safety function and its output "
                "is not stop evidence.")

# frames that come from the host side of each node, by profile (node_id is filled in)
HOST_IDS = {
    "zlac": lambda n: {0x000, 0x600 + n},
    "ctl017": lambda n: {0x000, 0x600 + n, 0x701, 0x210, 0x310},
}
DEFAULT_NODE_ID = {"zlac": 1, "ctl017": 0x10}
DEFAULT_VOLATILE = {"zlac": (0x606C, 0x6041, 0x603F), "ctl017": (0x2000, 0x2001, 0x2300)}


class TraceError(ValueError):
    pass


class Frame:
    __slots__ = ("t", "channel", "arb_id", "data", "extended", "remote")

    def __init__(self, t, channel, arb_id, data, extended=False, remote=False):
        self.t, self.channel, self.arb_id = float(t), channel, arb_id
        self.data, self.extended, self.remote = bytes(data), extended, remote

    def __eq__(self, other):
        return (isinstance(other, Frame) and self.arb_id == other.arb_id
                and self.data == other.data and self.extended == other.extended
                and self.remote == other.remote and self.channel == other.channel
                and abs(self.t - other.t) < 5e-7)

    def __repr__(self):
        return "Frame(%s)" % format_line(self)


def parse_line(line):
    """Parse one candump -L line into a Frame. Raises TraceError on anything else."""
    m = LINE.match(line.strip())
    if not m:
        raise TraceError("not a candump -L line: %r" % line.strip()[:80])
    t, channel, ident, payload = m.groups()
    if len(ident) not in (3, 8):
        raise TraceError("identifier must have 3 or 8 hex digits: %r" % ident)
    arb_id = int(ident, 16)
    extended = len(ident) == 8
    if (not extended and arb_id > 0x7FF) or arb_id > 0x1FFFFFFF:
        raise TraceError("identifier out of range: %r" % ident)
    if payload == "R":
        return Frame(t, channel, arb_id, b"", extended, True)
    if len(payload) % 2 or len(payload) > 16:
        raise TraceError("classic CAN payload must be 0 to 8 whole bytes: %r" % payload)
    return Frame(t, channel, arb_id, bytes.fromhex(payload), extended)


def format_line(frame):
    ident = "%08X" % frame.arb_id if frame.extended else "%03X" % frame.arb_id
    payload = "R" if frame.remote else frame.data.hex().upper()
    return "(%.6f) %s %s#%s" % (frame.t, frame.channel, ident, payload)


def read_trace(path, lenient=False):
    """Return (frames, errors). errors is a list of 'line N: message'; without lenient, any
    error raises TraceError and no frames are returned. Timestamps must not go backwards."""
    frames, errors = [], []
    with open(path, encoding="utf-8") as fh:
        for n, line in enumerate(fh, 1):
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            try:
                frame = parse_line(line)
            except TraceError as exc:
                errors.append("line %d: %s" % (n, exc))
                continue
            if frames and frame.t < frames[-1].t:
                errors.append("line %d: timestamp goes backwards" % n)
                continue
            frames.append(frame)
    if errors and not lenient:
        raise TraceError("; ".join(errors[:5]) + (" (+%d more)" % (len(errors) - 5)
                                                  if len(errors) > 5 else ""))
    return frames, errors


def write_trace(path, frames, header=None):
    with open(path, "w", encoding="utf-8") as fh:
        for line in (header or "").splitlines():
            fh.write("# %s\n" % line)
        for f in frames:
            fh.write(format_line(f) + "\n")


def record(bus, seconds, channel=None, stop=None):
    """Listen only. Returns the frames seen for `seconds` (or until stop() is true)."""
    channel = channel or getattr(bus, "channel_info", "can")
    frames = []
    end = time.monotonic() + seconds
    while time.monotonic() < end and not (stop and stop()):
        m = bus.recv(timeout=min(0.1, max(0.0, end - time.monotonic())))
        if m is None or m.is_error_frame:
            continue
        frames.append(Frame(m.timestamp or time.time(), channel, m.arbitration_id,
                            b"" if m.is_remote_frame else bytes(m.data),
                            m.is_extended_id, m.is_remote_frame))
    return frames


def send_frame(bus, f):
    bus.send(can.Message(arbitration_id=f.arb_id, data=f.data, is_extended_id=f.extended,
                         is_remote_frame=f.remote))


def replay(bus, frames, ids=None, speed=1.0, on_sent=None):
    """Transmit the frames (those whose ID is in `ids`, or all) with the recorded spacing divided
    by `speed`; speed 0 sends as fast as possible. Returns the number of frames sent."""
    chosen = [f for f in frames if ids is None or f.arb_id in ids]
    if not chosen:
        return 0
    start = time.monotonic()
    t0 = chosen[0].t
    for f in chosen:
        if speed > 0:
            delay = start + (f.t - t0) / speed - time.monotonic()
            if delay > 0:
                time.sleep(delay)
        send_frame(bus, f)
        if on_sent:
            on_sent(f)
    return len(chosen)


def sdo_kind(data):
    """('ack'|'upload'|'abort'|'other', index, sub, payload) of an SDO server frame."""
    d = bytes(data).ljust(8, b"\x00")
    index, sub = d[1] | d[2] << 8, d[3]
    if d[0] == 0x60:
        return "ack", index, sub, b""
    if d[0] in (0x4F, 0x4B, 0x43):
        size = {0x4F: 1, 0x4B: 2, 0x43: 4}[d[0]]
        return "upload", index, sub, d[4:4 + size]
    if d[0] == 0x80:
        return "abort", index, sub, d[4:8]
    return "other", index, sub, d[:]


def responses_in_trace(frames, node_id):
    """Pair every SDO request (0x600+n) with the first response (0x580+n) before the next one."""
    pairs = []
    for i, f in enumerate(frames):
        if f.arb_id != 0x600 + node_id or f.extended or f.remote:
            continue
        answer = None
        for g in frames[i + 1:]:
            if g.arb_id == 0x600 + node_id:
                break
            if g.arb_id == 0x580 + node_id:
                answer = g
                break
        pairs.append((f, answer))
    return pairs


def same_response(expected, got, exact, volatile):
    """Compare two SDO response frames (bytes). Returns None when they match, else a reason."""
    if expected is None and got is None:
        return None
    if expected is None:
        return "the trace has no response, the emulator answered"
    if got is None:
        return "no response from the emulator"
    ek, ei, es, ep = sdo_kind(expected)
    gk, gi, gs, gp = sdo_kind(got)
    if (ek, ei, es) != (gk, gi, gs):
        return "expected %s of %04Xh:%02X, got %s of %04Xh:%02X" % (ek, ei, es, gk, gi, gs)
    if ek == "abort" and ep != gp:
        return "abort code differs: expected 0x%08X, got 0x%08X" % (
            int.from_bytes(ep, "little"), int.from_bytes(gp, "little"))
    if ek == "upload" and len(ep) != len(gp):
        return "size differs"
    if ek == "upload" and exact and ei not in volatile and ep != gp:
        return "data differs: expected %s, got %s" % (ep.hex(), gp.hex())
    return None


def check_trace(bus, frames, node_id, profile="zlac", exact=False, volatile=None, speed=1.0,
                response_timeout=0.5):
    """Replay the host side of a trace into an emulator on `bus` and compare its SDO responses
    with the trace. Returns a list of problems; an empty list means the run matches."""
    volatile = set(DEFAULT_VOLATILE[profile] if volatile is None else volatile)
    host = HOST_IDS[profile](node_id)
    pairs = {id(req): ans for req, ans in responses_in_trace(frames, node_id)}
    problems = []
    n_requests = 0
    start = time.monotonic()
    t0 = frames[0].t if frames else 0.0
    for f in frames:
        if f.arb_id not in host:
            continue
        if speed > 0:
            delay = start + (f.t - t0) / speed - time.monotonic()
            if delay > 0:
                time.sleep(delay)
        while bus.recv(timeout=0) is not None:
            pass  # stale node traffic
        send_frame(bus, f)
        if id(f) not in pairs:
            continue
        n_requests += 1
        got = None
        end = time.monotonic() + response_timeout
        while time.monotonic() < end:
            m = bus.recv(timeout=max(0.0, end - time.monotonic()))
            if m is not None and m.arbitration_id == 0x580 + node_id and not m.is_extended_id:
                got = bytes(m.data)
                break
        why = same_response(pairs[id(f)].data if pairs[id(f)] else None, got, exact, volatile)
        if why:
            problems.append("t=%.3f request %s: %s" % (f.t - t0, f.data.hex().upper(), why))
    if n_requests == 0:
        problems.append("the trace holds no SDO request to node %d" % node_id)
    return problems


def _require_safe_interface(channel, ok):
    if channel.startswith("vcan"):
        return
    print(BENCH_NOTICE, file=sys.stderr)
    if not ok:
        print("refusing to transmit on %s: pass --i-am-at-the-bench once the owner's bench "
              "procedure is in hand and a second person stands at the E-stop" % channel,
              file=sys.stderr)
        sys.exit(2)


def _ids(text):
    return {int(x, 16) for x in text.split(",") if x.strip()} if text else None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("record")
    r.add_argument("channel")
    r.add_argument("out")
    r.add_argument("--seconds", type=float, default=10.0)
    p = sub.add_parser("replay")
    p.add_argument("trace")
    p.add_argument("channel")
    p.add_argument("--ids", help="comma separated hex CAN IDs to send (default all)")
    p.add_argument("--speed", type=float, default=1.0, help="1 = recorded timing, 0 = no delay")
    p.add_argument("--lenient", action="store_true")
    p.add_argument("--i-am-at-the-bench", action="store_true")
    c = sub.add_parser("check")
    c.add_argument("trace")
    c.add_argument("channel")
    c.add_argument("--node", choices=sorted(HOST_IDS), required=True)
    c.add_argument("--node-id", type=lambda s: int(s, 0))
    c.add_argument("--exact", action="store_true")
    c.add_argument("--volatile", help="comma separated hex object indexes not compared by data")
    c.add_argument("--speed", type=float, default=1.0)
    c.add_argument("--lenient", action="store_true")
    c.add_argument("--i-am-at-the-bench", action="store_true")
    args = ap.parse_args(argv)
    if args.cmd == "record":
        bus = can.Bus(interface="socketcan", channel=args.channel)
        try:
            frames = record(bus, args.seconds, args.channel)
        finally:
            bus.shutdown()
        write_trace(args.out, frames)
        print("recorded %d frames to %s" % (len(frames), args.out))
        return 0
    _require_safe_interface(args.channel, args.i_am_at_the_bench)
    try:
        frames, errors = read_trace(args.trace, args.lenient)
    except (TraceError, OSError) as exc:
        print("cannot read trace: %s" % exc, file=sys.stderr)
        return 2
    for e in errors:
        print("skipped " + e, file=sys.stderr)
    bus = can.Bus(interface="socketcan", channel=args.channel)
    try:
        if args.cmd == "replay":
            n = replay(bus, frames, _ids(args.ids), args.speed)
            print("sent %d frames" % n)
            return 0
        node_id = args.node_id or DEFAULT_NODE_ID[args.node]
        vol = None if args.volatile is None else {int(x, 16) for x in args.volatile.split(",")}
        problems = check_trace(bus, frames, node_id, args.node, args.exact, vol, args.speed)
    finally:
        bus.shutdown()
    for p_ in problems:
        print("DIFF " + p_)
    print("FAIL" if problems else "PASS")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
