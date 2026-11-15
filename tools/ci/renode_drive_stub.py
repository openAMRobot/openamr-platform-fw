# SPDX-License-Identifier: MIT
# Drive stub for the telemetry image, loaded INTO Renode (IronPython 2.7) by
# tools/ci/renode_telemetry.py with "include @renode_drive_stub.py".
#
# It is a CANopen node (default node 1) attached to the Renode CAN hub next to
# FDCAN1 of the image. It answers expedited SDO uploads from a small object
# table, sends a boot-up message and heartbeats, and can misbehave on request
# (stay silent, stop heartbeats, abort one object, duplicate or delay answers).
# All its timing is Renode virtual time.
#
# It is a test fixture, not a model of the ZLAC8015D: the values and the
# answer delay are made up. The ZLAC emulator of platform-fw #38
# (tools/can_sil) was not reused: it needs a Linux vcan interface and wall
# clock threads, cannot attach to a Renode CAN hub, and has no object 2026h.
#
# The stub also checks the read-only boundary from the outside: every frame
# the image sends is inspected, and anything that is not an SDO
# initiate-upload request to the node is recorded as a violation.
#
# Monitor commands (after the machine and the hub exist):
#   tlm_stub_attach "hubName"        create the stub and attach it to the hub
#   tlm_stub_set "key" value         see CONFIG below
#   tlm_stub_object index sub size value
#   tlm_stub_tfluna "sysbus.usart2" period_ms distance_cm strength   (0 ms = stop)
#   tlm_stub_mb7060 "sysbus.uart4" period_ms range_cm                (0 ms = stop)
#   tlm_stub_uart_burst "sysbus.usart2" frames distance_cm strength  TF-Luna frames back to back, now
#   tlm_stub_report                  print one "TLM_STUB {json}" line
#   tlm_aux_attach "can2" "can3"     create the BMS stub on hub can2 and the CTL-017 stub on can3
#   tlm_lift_set "key" value         see LIFT below (state, flags, position, period_ms, ...)
#   tlm_bms_set "key" value          see BMS below (period_ms, ...)
#   (the aux stubs record every frame the image puts on CAN2 or CAN3: none is allowed at all)
#   tlm_throttle_start ms            virtual time may not run ahead of wall time by more than one
#                                    slice of ms (micro-ROS scenarios: the agent runs in wall time)
#   tlm_time_report                  print one "TLM_TIME {json}" line: virtual and wall time
#   tlm_stub_clock                   install the SystemCoreClock hook again (after "machine Reset",
#                                    which drops bus hooks: see renode_telemetry.reset_steps)
#   tlm_stub_cpu_report              print one "TLM_CPU {json}" line: executed instructions,
#                                    virtual time and the CPU's MIPS setting
from Antmicro.Renode.Core import EmulationManager
from Antmicro.Renode.Core.CAN import CANMessageFrame
from Antmicro.Renode.Core.Structure import NullRegistrationPoint
from Antmicro.Renode.Peripherals.Bus import Access, BusHookDelegate, SysbusAccessWidth
from Antmicro.Renode.Peripherals.CAN import ICAN
from Antmicro.Renode.Time import TimeInterval
from Antmicro.Renode.Tools.Network import CANHub
from System import Action, Array, Byte, UInt64

ABORT_NO_OBJECT = 0x06020000

CONFIG = {
    "node": 1,
    "silent": 0,            # 1: answer nothing and send no heartbeat
    "heartbeat_ms": 100,    # 0: no heartbeat
    "response_us": 500,     # answer delay
    "abort_index": 0,       # with abort_sub: this object answers with an abort
    "abort_sub": 0,
    "duplicate": 0,         # 1: every answer is sent twice, 200 us apart
    "late_index": 0,        # with late_sub: this object answers after late_us
    "late_sub": 0,
    "late_us": 50000,
    "burst_n": 0,           # with burst_ms: every burst_ms, burst_n heartbeat frames at the same instant
    "burst_ms": 0,
}


# CTL-017 stub (openamr-upperbody-fw PR #17 dictionary, node 0x10): heartbeat 0x710, TPDO1 0x190,
# TPDO2 0x290 every period_ms, EMCY 0x090 on request. Values are test inputs.
LIFT = {"node": 0x10, "period_ms": 100, "hb_ms": 100, "nmt": 0x05, "state": 3, "flags": 0x03,
        "position": 5000, "active_stop": 0, "velocity": 0, "current_ma": 0, "faults": 0, "aux": 0,
        "skip_counter": 0, "silent": 0}
# Daly stub: one 0x90 SOC frame from BMS 0x01 to host 0x40 every period_ms (daly_rx.h layout).
BMS = {"period_ms": 200, "bms": 0x01, "host": 0x40, "pack_dv": 256, "current_raw": 30000, "soc_pm": 800,
       "silent": 0}


class AuxStub(ICAN):
    """A node on CAN2 or CAN3 that only sends. Every frame it receives came from the image."""

    def __init__(self, machine, name):
        self.machine = machine
        self.name = name
        self.handlers = []
        self.received = []
        self.frames_sent = 0
        self.generation = 0

    def add_FrameSent(self, handler):
        self.handlers.append(handler)

    def remove_FrameSent(self, handler):
        self.handlers.remove(handler)

    def Reset(self):
        pass

    def OnFrameReceived(self, frame):
        self.received.append("%s id 0x%X data %s" % (self.name, frame.Id, " ".join("%02X" % int(b) for b in frame.Data)))

    def transmit(self, can_id, data, extended=False):
        self.frames_sent += 1
        frame = CANMessageFrame(can_id, Array[Byte](data), extended, False)
        for handler in list(self.handlers):
            handler(frame)

    def every(self, ms, fn):
        self.generation += 1
        generation = self.generation

        def run():
            if generation != self.generation:
                return
            fn()
            self.machine.ScheduleAction(TimeInterval.FromMicroseconds(UInt64(ms * 1000)),
                                        Action[TimeInterval](lambda _ts: run()))
        self.machine.ScheduleAction(TimeInterval.FromMicroseconds(UInt64(ms * 1000)),
                                    Action[TimeInterval](lambda _ts: run()))


class LiftStub(AuxStub):
    def __init__(self, machine):
        AuxStub.__init__(self, machine, "can3")
        self.counter = 0
        self.tpdo1 = 0

    def start(self):
        def tick():
            if LIFT["silent"]:
                return
            n = LIFT["node"]
            self.transmit(0x700 + n, [LIFT["nmt"]])
            p = LIFT["position"] & 0xFFFF
            v = LIFT["velocity"] & 0xFFFF
            self.counter = (self.counter + 1 + (1 if LIFT["skip_counter"] else 0)) & 0xFF
            LIFT["skip_counter"] = 0
            self.tpdo1 += 1
            self.transmit(0x180 + n, [LIFT["state"], LIFT["flags"], p & 0xFF, p >> 8, LIFT["active_stop"], 0, 0xFF,
                                      self.counter])
            c = LIFT["current_ma"] & 0xFFFF
            self.transmit(0x280 + n, [0x10, 0xFF, c & 0xFF, c >> 8, v & 0xFF, v >> 8, LIFT["faults"] & 0xFF,
                                      LIFT["aux"] & 0xFF])
        self.every(LIFT["period_ms"], tick)


class BmsStub(AuxStub):
    def __init__(self, machine):
        AuxStub.__init__(self, machine, "can2")

    def start(self):
        def tick():
            if BMS["silent"]:
                return
            ident = (0x18 << 24) | (0x90 << 16) | (BMS["host"] << 8) | BMS["bms"]
            pv, cur, soc = BMS["pack_dv"], BMS["current_raw"], BMS["soc_pm"]
            self.transmit(ident, [pv >> 8, pv & 0xFF, 0, 0, cur >> 8, cur & 0xFF, soc >> 8, soc & 0xFF], True)
        self.every(BMS["period_ms"], tick)


AUX = {}


class DriveStub(ICAN):
    def __init__(self, machine):
        self.machine = machine
        self.handlers = []
        self.objects = {}        # (index, sub) -> (size, value)
        self.requests = 0
        self.responses = 0
        self.heartbeats = 0
        self.violations = []
        self.other_frames = 0
        self.hb_generation = 0
        self.burst_generation = 0
        self.frames_sent = 0     # every frame the stub put on the hub
        self.feeders = {}
        self.clock_hooks = 1     # hook installs: 1 at attach, +1 per tlm_stub_clock

    # ---- ICAN ----
    def add_FrameSent(self, handler):
        self.handlers.append(handler)

    def remove_FrameSent(self, handler):
        self.handlers.remove(handler)

    def Reset(self):
        pass

    def OnFrameReceived(self, frame):
        data = [int(b) for b in frame.Data]
        node = CONFIG["node"]
        is_upload = (not frame.ExtendedFormat and not frame.RemoteFrame and frame.Id == 0x600 + node
                     and len(data) == 8 and data[0] == 0x40 and data[4:] == [0, 0, 0, 0])
        if not is_upload:
            # Anything else from the image breaks its read-only boundary.
            self.violations.append("id 0x%X data %s" % (frame.Id, " ".join("%02X" % b for b in data)))
            return
        self.requests += 1
        if CONFIG["silent"]:
            return
        index = data[1] | (data[2] << 8)
        sub = data[3]
        delay = CONFIG["response_us"]
        if index == CONFIG["late_index"] and sub == CONFIG["late_sub"] and index != 0:
            delay = CONFIG["late_us"]
        if index == CONFIG["abort_index"] and sub == CONFIG["abort_sub"] and index != 0:
            payload = [0x80, data[1], data[2], sub] + le(ABORT_NO_OBJECT, 4)
        elif (index, sub) in self.objects:
            size, value = self.objects[(index, sub)]
            payload = [0x43 | ((4 - size) << 2), data[1], data[2], sub] + le(value, size) + [0] * (4 - size)
        else:
            payload = [0x80, data[1], data[2], sub] + le(ABORT_NO_OBJECT, 4)
        self.after(delay, lambda: self.respond(payload))
        if CONFIG["duplicate"]:
            self.after(delay + 200, lambda: self.respond(payload))

    # ---- helpers ----
    def after(self, micros, fn):
        self.machine.ScheduleAction(TimeInterval.FromMicroseconds(UInt64(micros)),
                                    Action[TimeInterval](lambda _ts: fn()))

    def transmit(self, can_id, data):
        self.frames_sent += 1
        frame = CANMessageFrame(can_id, Array[Byte](data), False, False)
        for handler in list(self.handlers):
            handler(frame)

    def respond(self, payload):
        if CONFIG["silent"]:
            return
        self.responses += 1
        self.transmit(0x580 + CONFIG["node"], payload)

    def start_heartbeat(self):
        self.hb_generation += 1
        generation = self.hb_generation
        if CONFIG["silent"] or CONFIG["heartbeat_ms"] <= 0:
            return
        def bootup():
            if generation == self.hb_generation and not CONFIG["silent"]:
                self.transmit(0x700 + CONFIG["node"], [0x00])
        # Sent from the machine's time domain, like every other frame of the stub: 50 ms after the
        # stub starts (a boot-up sent before the image has started FDCAN1 is not received; the
        # image's start-up initialises FDCAN1, FDCAN2 and FDCAN3 first), 1 ms after a restart.
        self.after(50000 if self.hb_generation == 1 else 1000, bootup)

        def beat():
            if generation != self.hb_generation or CONFIG["silent"] or CONFIG["heartbeat_ms"] <= 0:
                return
            self.heartbeats += 1
            self.transmit(0x700 + CONFIG["node"], [0x05])   # operational
            self.after(CONFIG["heartbeat_ms"] * 1000, beat)
        self.after(CONFIG["heartbeat_ms"] * 1000, beat)

    def start_bursts(self):
        # Sustained traffic of accepted frames: burst_n heartbeats (700h + node, operational)
        # at one instant, every burst_ms. They pass the image's filter like real heartbeats.
        self.burst_generation += 1
        generation = self.burst_generation
        if CONFIG["burst_n"] <= 0 or CONFIG["burst_ms"] <= 0:
            return

        def burst():
            if generation != self.burst_generation or CONFIG["silent"]:
                return
            for _ in range(CONFIG["burst_n"]):
                self.transmit(0x700 + CONFIG["node"], [0x05])
            self.after(CONFIG["burst_ms"] * 1000, burst)
        self.after(CONFIG["burst_ms"] * 1000, burst)

    def feed(self, uart_name, period_ms, frame_bytes):
        gen = self.feeders.get(uart_name, 0) + 1
        self.feeders[uart_name] = gen
        if period_ms <= 0:
            return
        uart = self.machine[uart_name]

        def send():
            if self.feeders.get(uart_name) != gen:
                return
            for b in frame_bytes:
                uart.WriteChar(b)
            self.after(period_ms * 1000, send)
        self.after(period_ms * 1000, send)


def le(value, size):
    value &= 0xFFFFFFFF
    return [(value >> (8 * i)) & 0xFF for i in range(size)]


STUB = [None]


def _machine():
    return list(EmulationManager.Instance.CurrentEmulation.Machines)[0]


def mc_tlm_stub_attach(hub_name):
    emulation = EmulationManager.Instance.CurrentEmulation
    machine = _machine()
    hub = [h for h in emulation.ExternalsManager.GetExternalsOfType[CANHub]()][0]
    stub = DriveStub(machine)
    # The hub looks up the machine of every attached interface, so the stub is
    # registered as a peripheral without a bus address.
    machine.SystemBus.Register(stub, NullRegistrationPoint.Instance)
    machine.SetLocalName(stub, "driveStub")
    hub.AttachTo(stub)
    stub.objects = {
        (0x606C, 1): (4, 1234),          # actual velocity channel 1, drive units
        (0x606C, 2): (4, -1234),         # channel 2
        (0x6041, 0): (2, 0x0237),        # statusword: operation enabled
        (0x603F, 0): (4, 0),             # fault code
        (0x2026, 5): (2, 1),             # speed resolution; size is a guess (not in the vendor notes)
    }
    STUB[0] = stub
    _calibrate_systick(machine)
    stub.start_heartbeat()


def _calibrate_systick(machine):
    # Renode's STM32H7 RCC model does not keep the PLL and divider registers,
    # so the HAL derives a SystemCoreClock that is not the 192 MHz of the
    # board, and programs SysTick from it. To keep "1 tick = 1 ms of virtual
    # time", the SysTick input frequency of the model follows every value the
    # firmware writes to SystemCoreClock. Clock tree, baud rates and CAN bit
    # timing are therefore NOT checked by these tests.
    bus = machine.SystemBus
    address = bus.GetSymbolAddress("SystemCoreClock")
    nvic = machine["sysbus.nvic"]

    def on_write(cpu, addr, width, value):
        if value != 0:
            nvic.Frequency = int(value)
            print "TLM_STUB_CLOCK SystemCoreClock=%d (SysTick model frequency set to match)" % value
    bus.AddWatchpointHook(address, SysbusAccessWidth.DoubleWord, Access.Write,
                          BusHookDelegate(on_write))


def mc_tlm_stub_clock():
    # "machine Reset" drops bus hooks and puts the NVIC model back to its default SysTick
    # frequency; the image writes SystemCoreClock again while it boots, so the hook is installed
    # again before the machine runs (the caller pauses first).
    STUB[0].clock_hooks += 1
    _calibrate_systick(_machine())


def mc_tlm_stub_cpu_report():
    machine = _machine()
    cpu = machine["sysbus.cpu"]
    virtual_s = machine.LocalTimeSource.ElapsedVirtualTime.TotalSeconds
    print 'TLM_CPU {"instructions":%d,"virtual_s":%.6f,"mips":%d}' % (
        int(cpu.ExecutedInstructions), virtual_s, int(cpu.PerformanceInMips))


THROTTLE = {"generation": 0, "slice_us": 5000, "virtual_us": 0, "wall": None, "watch": None}


def mc_tlm_throttle_start(slice_ms):
    # Every slice of virtual time takes at least the same slice of wall time: the emulation thread
    # sleeps at the end of a slice that ran faster. Renode may run slower than wall time, never
    # faster by more than one slice. Scheduled actions do not survive "machine Reset".
    from System.Diagnostics import Stopwatch
    from System.Threading import Thread
    machine = _machine()
    THROTTLE["generation"] += 1
    generation = THROTTLE["generation"]
    THROTTLE["slice_us"] = int(slice_ms) * 1000
    watch = Stopwatch.StartNew()
    if THROTTLE["wall"] is None:
        THROTTLE["wall"] = Stopwatch.StartNew()

    def tick(_ts):
        if generation != THROTTLE["generation"]:
            return
        THROTTLE["virtual_us"] += THROTTLE["slice_us"]
        ahead_ms = THROTTLE["slice_us"] / 1000 - watch.ElapsedMilliseconds
        if ahead_ms > 0:
            Thread.Sleep(int(ahead_ms))
        watch.Restart()
        machine.ScheduleAction(TimeInterval.FromMicroseconds(UInt64(THROTTLE["slice_us"])), Action[TimeInterval](tick))
    machine.ScheduleAction(TimeInterval.FromMicroseconds(UInt64(THROTTLE["slice_us"])), Action[TimeInterval](tick))


def mc_tlm_time_report():
    machine = _machine()
    wall = THROTTLE["wall"]
    print 'TLM_TIME {"virtual_s":%.6f,"wall_s":%.6f}' % (
        machine.LocalTimeSource.ElapsedVirtualTime.TotalSeconds, (wall.ElapsedMilliseconds / 1000.0) if wall else 0.0)


def mc_tlm_stub_set(key, value):
    if key not in CONFIG:
        raise Exception("unknown stub setting " + key)
    CONFIG[key] = int(value)
    if key in ("heartbeat_ms", "silent"):
        STUB[0].start_heartbeat()
    if key in ("burst_n", "burst_ms"):
        STUB[0].start_bursts()


def mc_tlm_stub_object(index, sub, size, value):
    STUB[0].objects[(int(index), int(sub))] = (int(size), int(value))


def mc_tlm_stub_tfluna(uart_name, period_ms, distance_cm, strength):
    d, a, t = int(distance_cm), int(strength), 0x0900   # 0x0900 / 8 - 256 = 32 degC
    body = [0x59, 0x59, d & 0xFF, d >> 8, a & 0xFF, a >> 8, t & 0xFF, t >> 8]
    STUB[0].feed(uart_name, int(period_ms), body + [sum(body) & 0xFF])


def tfluna_frame(distance_cm, strength):
    d, a, t = int(distance_cm), int(strength), 0x0900   # 0x0900 / 8 - 256 = 32 degC
    body = [0x59, 0x59, d & 0xFF, d >> 8, a & 0xFF, a >> 8, t & 0xFF, t >> 8]
    return body + [sum(body) & 0xFF]


def mc_tlm_stub_uart_burst(uart_name, frames, distance_cm, strength):
    # Many TF-Luna frames at one instant: more bytes than the image's receive ring holds.
    uart = _machine()[uart_name]
    data = tfluna_frame(distance_cm, strength) * int(frames)
    for b in data:
        uart.WriteChar(b)
    print "TLM_STUB_BURST %s %d bytes" % (uart_name, len(data))


def mc_tlm_stub_mb7060(uart_name, period_ms, range_cm):
    STUB[0].feed(uart_name, int(period_ms), [ord(c) for c in "R%03d\r" % int(range_cm)])


def mc_tlm_aux_attach(bms_hub, lift_hub):
    emulation = EmulationManager.Instance.CurrentEmulation
    machine = _machine()
    for key, cls, hub_name in (("bms", BmsStub, bms_hub), ("lift", LiftStub, lift_hub)):
        found, hub = emulation.ExternalsManager.TryGetByName[CANHub](hub_name)
        if not found:
            raise Exception("no CAN hub named " + hub_name)
        stub = cls(machine)
        machine.SystemBus.Register(stub, NullRegistrationPoint.Instance)
        machine.SetLocalName(stub, key + "Stub")
        hub.AttachTo(stub)
        AUX[key] = stub
        stub.start()


def mc_tlm_aux_restart():
    # Scheduled actions do not survive "machine Reset": start the periodic frames again.
    for stub in AUX.values():
        stub.start()


def mc_tlm_lift_set(key, value):
    if key not in LIFT:
        raise Exception("unknown lift stub setting " + key)
    LIFT[key] = int(value)


def mc_tlm_bms_set(key, value):
    if key not in BMS:
        raise Exception("unknown BMS stub setting " + key)
    BMS[key] = int(value)


def mc_tlm_stub_report():
    for key in ("bms", "lift"):
        if key in AUX:
            a = AUX[key]
            print 'TLM_AUX {"bus":"%s","frames_sent":%d,"received":%d}' % (a.name, a.frames_sent, len(a.received))
            for v in a.received[:20]:
                print "TLM_STUB_VIOLATION " + v
    s = STUB[0]
    print 'TLM_STUB {"requests":%d,"responses":%d,"heartbeats":%d,"frames_sent":%d,"violations":%d,"clock_hooks":%d}' % (
        s.requests, s.responses, s.heartbeats, s.frames_sent, len(s.violations), s.clock_hooks)
    for v in s.violations[:20]:
        print "TLM_STUB_VIOLATION " + v
