"""SdoClient matches each answer to its request (issue #62), on the injectable clock of #58.

These tests run on python-can's in-process virtual bus, so they need no vcan interface. They
check the client's matching logic only; they are not vcan or bench evidence. Each test fails
on the previous client, which took the first frame from 580h + node as the answer."""
import itertools
import struct
import threading
import time

import can
import pytest

from canopen_common import SdoAbort, SdoClient

NODE = 1
RX, TX = 0x600 + NODE, 0x580 + NODE
_channel = itertools.count()


def upload(index, sub, value):
    return bytes([0x43]) + struct.pack("<HBi", index, sub, value)


def confirm(index, sub):
    return struct.pack("<BHB4x", 0x60, index, sub)


def abort(index, sub, code=0x06020000):
    return struct.pack("<BHBI", 0x80, index, sub, code)


class ScriptedServer:
    """For every request, script(n, cs, index, sub) gives the frames to send:
    (delay_s, data) or (delay_s, data, arbitration_id, extended)."""

    def __init__(self, bus, script):
        self.bus, self.script = bus, script
        self.requests = 0
        self._running = True
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        while self._running:
            msg = self.bus.recv(timeout=0.002)
            if msg is None or msg.arbitration_id != RX:
                continue
            cs = msg.data[0]
            index, sub = struct.unpack("<HB", bytes(msg.data[1:4]))
            n, self.requests = self.requests, self.requests + 1
            for frame in self.script(n, cs, index, sub):
                if not self._running:
                    return
                if frame[0]:
                    time.sleep(frame[0])
                arb = frame[2] if len(frame) > 2 else TX
                ext = frame[3] if len(frame) > 3 else False
                self.bus.send(can.Message(arbitration_id=arb, data=frame[1], is_extended_id=ext))

    def stop(self):
        self._running = False
        self._thread.join(timeout=1)


@pytest.fixture
def pair():
    channel = "sdo-match-%d" % next(_channel)
    buses, servers = [], []

    def factory(script, timeout=0.1, now=None):
        server_bus = can.Bus(interface="virtual", channel=channel)
        client_bus = can.Bus(interface="virtual", channel=channel)
        buses.extend((server_bus, client_bus))
        servers.append(ScriptedServer(server_bus, script))
        if now is None:
            return SdoClient(client_bus, NODE, timeout=timeout)
        return SdoClient(client_bus, NODE, timeout=timeout, now=now)
    yield factory
    for server in servers:
        server.stop()
    for bus in buses:
        bus.shutdown()


def test_read_takes_only_an_upload_answer_of_its_own_object(pair):
    sdo = pair(lambda n, cs, index, sub: [
        (0, confirm(index, sub)),                  # write confirmation (KeyError 96 before)
        (0, upload(index, sub + 1, 7)),            # other subindex
        (0, upload(index, sub, 99)[:7]),           # short DLC
        (0, upload(index, sub, 98), TX, True),     # extended id, not the SDO server
        (0, upload(index, sub, 97), TX + 1),       # node 2
        (0, upload(index, sub, 42))])
    assert sdo.read(0x606C, 1, signed=True) == 42
    assert sdo.stale_replies == 3


def test_write_takes_only_its_own_confirmation_and_the_next_read_its_own_answer(pair):
    def script(n, cs, index, sub):
        if cs == 0x40:
            return [(0, upload(index, sub, 5))]
        return [(0, upload(index, sub, 1)), (0, confirm(index + 1, sub)), (0, confirm(index, sub))]
    sdo = pair(script)
    sdo.write(0x60FF, 1, 19, 4, signed=True)
    assert sdo.read(0x606C, 1, signed=True) == 5
    assert sdo.stale_replies == 2


def test_an_abort_of_another_object_is_dropped_and_an_abort_of_the_request_raises(pair):
    def script(n, cs, index, sub):
        if index == 0x603F:
            return [(0, abort(index, sub, 0x06090011))]
        return [(0, abort(0x6041, 0)), (0, upload(index, sub, 11))]
    sdo = pair(script)
    assert sdo.read(0x606C, 2, signed=True) == 11
    with pytest.raises(SdoAbort) as err:
        sdo.read(0x603F, 0)
    assert err.value.code == 0x06090011


def test_a_short_abort_frame_is_dropped_and_the_request_times_out(pair):
    sdo = pair(lambda n, cs, index, sub: [(0, abort(index, sub)[:4])], timeout=0.05)
    with pytest.raises(TimeoutError):
        sdo.read(0x606C, 1)
    assert sdo.stale_replies == 1


def test_duplicated_answers_are_not_taken_by_the_next_request(pair):
    values = {(0x606C, 1): 100, (0x606C, 2): -200}

    def script(n, cs, index, sub):
        frame = upload(index, sub, values[(index, sub)]) if cs == 0x40 else confirm(index, sub)
        return [(0, frame), (0, frame)]
    sdo = pair(script)
    for _ in range(3):
        assert sdo.read(0x606C, 1, signed=True) == 100
        assert sdo.read(0x606C, 2, signed=True) == -200
        sdo.write(0x60FF, 1, 19, 4, signed=True)


def test_a_late_answer_after_the_next_request_is_not_its_answer(pair):
    def script(n, cs, index, sub):
        if n == 0:
            return []                              # held back past the client timeout
        if n == 1:
            return [(0, upload(0x606C, 1, 100)), (0.005, upload(index, sub, -200))]
        return []
    sdo = pair(script, timeout=0.05)
    with pytest.raises(TimeoutError):
        sdo.read(0x606C, 1, signed=True)
    assert sdo.read(0x606C, 2, signed=True) == -200
    assert sdo.stale_replies == 1


# --- matching on the injected clock of #58 ------------------------------------------------------

class Scaled:
    """`rate` seconds of client clock per second of host time; rate 0 is a frozen clock."""

    def __init__(self, rate, t=50.0):
        self.rate, self.t0, self.host0 = rate, t, None

    def now(self):
        if self.host0 is None:
            self.host0 = time.monotonic()
        return self.t0 + self.rate * (time.monotonic() - self.host0)


def stale_stream(period_s=0.005, count=400):
    """Frames from the server that are never the answer: a 60h confirmation of another object."""
    return [(period_s, confirm(0x2000, 1)) for _ in range(count)]


def test_on_a_frozen_clock_stale_frames_are_dropped_and_the_late_answer_is_taken(pair):
    """#58: a paused clock still accepts a late reply. #62: only the matching one."""
    sdo = pair(lambda n, cs, index, sub: [(0.05, confirm(index, sub)), (0.05, upload(0x6041, 0, 1)),
                                          (0.3, upload(index, sub, 42))], timeout=0.2, now=Scaled(0.0).now)
    assert sdo.read(0x606C, 1, signed=True) == 42          # 0.4 s of host time, 0 s of client time
    assert sdo.stale_replies == 2


def test_a_stream_of_stale_frames_does_not_extend_the_deadline(pair):
    sdo = pair(lambda n, cs, index, sub: stale_stream(), timeout=0.1)
    start = time.monotonic()
    with pytest.raises(TimeoutError):
        sdo.read(0x606C, 1)
    assert time.monotonic() - start < 0.3
    assert sdo.stale_replies >= 5


def test_on_a_faster_clock_the_deadline_fires_while_stale_frames_arrive(pair):
    """Clock at 10x host: 0.5 s of client time is 0.05 s of host time, re-checked between frames."""
    sdo = pair(lambda n, cs, index, sub: stale_stream(), timeout=0.5, now=Scaled(10.0).now)
    start = time.monotonic()
    with pytest.raises(TimeoutError):
        sdo.read(0x606C, 1)
    assert time.monotonic() - start < 0.2


def test_on_a_slower_clock_a_matching_answer_late_in_host_time_is_taken_after_stale_frames(pair):
    """Clock at 0.1x host: 0.2 s of client time is 2 s of host time; the answer comes after 1 s of host."""
    sdo = pair(lambda n, cs, index, sub: [(0.2, upload(index, sub + 1, 5)), (0.8, upload(index, sub, -9))],
               timeout=0.2, now=Scaled(0.1).now)
    assert sdo.read(0x606C, 2, signed=True) == -9
    assert sdo.stale_replies == 1


def test_the_receive_wait_stays_a_short_host_bound():
    assert SdoClient.RECV_POLL_S <= 0.01
