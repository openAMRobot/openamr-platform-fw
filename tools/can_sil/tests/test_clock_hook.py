"""Injectable clock (issue #57): CanopenNode._now() and SdoClient(now=...).

A fake clock stands in for the simulator clock. Host time only bounds the waits.
"""
import threading
import time

import can
import pytest

from canopen_common import SDO_RX_BASE, SDO_TX_BASE, CanopenNode, SdoClient
from conftest import wait_for
from zlac8015d_emulator import Zlac8015dEmulator


class FakeClock:
    def __init__(self, t=100.0):
        self._t = t
        self._lock = threading.Lock()

    def now(self):
        with self._lock:
            return self._t

    def advance(self, dt):
        with self._lock:
            self._t += dt


class FakeClockZlac(Zlac8015dEmulator):
    def __init__(self, bus, clock, **kw):
        self.clock = clock  # set first: the emulator reads _now() in __init__
        super().__init__(bus, **kw)

    def _now(self):
        return self.clock.now()


@pytest.fixture
def clock():
    return FakeClock()


@pytest.fixture
def fake_zlac(make_bus, clock):
    node = FakeClockZlac(make_bus(), clock, node_id=1).start()
    yield node
    node.stop()


def enable_and_run(sdo, rpm):
    sdo.write(0x6060, 0, 3, 1, signed=True)
    for cw in (0x06, 0x07, 0x0F):
        sdo.write(0x6040, 0, cw, 2)
    sdo.write(0x60FF, 1, rpm, 4, signed=True)
    sdo.write(0x60FF, 2, rpm, 4, signed=True)


def test_link_loss_follows_the_node_clock(master, fake_zlac, clock):
    sdo = SdoClient(master, 1)
    sdo.write(0x2000, 0, 250, 2)
    enable_and_run(sdo, 100)
    # fake clock frozen: four times the 2000h timeout on the host, no trip
    time.sleep(1.0)
    assert not fake_zlac.link_lost
    assert fake_zlac.link_loss_events == 0
    # below the timeout on the fake clock: still no trip
    clock.advance(0.2)
    time.sleep(0.1)
    assert not fake_zlac.link_lost
    # past the timeout on the fake clock: trips once
    clock.advance(0.1)
    assert wait_for(lambda: fake_zlac.link_lost, timeout=1.0)
    assert fake_zlac.link_loss_events == 1
    assert fake_zlac.target_rpm(0) == 0.0


def test_tick_dt_follows_the_node_clock(master, fake_zlac, clock):
    sdo = SdoClient(master, 1)
    enable_and_run(sdo, 100)
    time.sleep(0.5)  # frozen clock: dt is 0, the 500 ms ramp does not move
    assert fake_zlac.rpm == [0.0, 0.0]
    clock.advance(1.0)  # longer than the ramp on the fake clock
    assert wait_for(lambda: fake_zlac.rpm == [100.0, 100.0], timeout=1.0)


def test_sdo_deadline_follows_the_client_clock(master):
    t = [0.0]

    def jumping():  # every read is one second later
        t[0] += 1.0
        return t[0]

    sdo = SdoClient(master, 9, timeout=0.5, now=jumping)  # no node 9 on the bus
    start = time.monotonic()
    with pytest.raises(TimeoutError):
        sdo.read(0x1000, 0)
    assert time.monotonic() - start < 0.2  # host default would wait the full 0.5 s


def test_sdo_late_reply_accepted_while_client_clock_is_frozen(master, make_bus):
    responder = make_bus()
    stop = threading.Event()

    def reply_late():
        while not stop.is_set():
            msg = responder.recv(timeout=0.05)
            if msg is not None and msg.arbitration_id == SDO_RX_BASE + 9:
                time.sleep(0.5)  # longer than the host-time timeout below
                responder.send(can.Message(arbitration_id=SDO_TX_BASE + 9,
                                           data=bytes([0x4F, 0x00, 0x10, 0x00, 7, 0, 0, 0]),
                                           is_extended_id=False))
                return

    th = threading.Thread(target=reply_late, daemon=True)
    th.start()
    frozen = FakeClock(5.0)
    sdo = SdoClient(master, 9, timeout=0.2, now=frozen.now)
    try:
        assert sdo.read(0x1000, 0) == 7
    finally:
        stop.set()
        th.join(timeout=2)


def test_default_clock_is_host_monotonic(master):
    assert SdoClient(master, 1)._now is time.monotonic
    before = time.monotonic()
    assert before <= CanopenNode._now() <= time.monotonic()
    assert Zlac8015dEmulator._now is CanopenNode._now
