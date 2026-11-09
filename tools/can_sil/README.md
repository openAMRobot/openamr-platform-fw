# CAN software-in-the-loop bench

Linux `vcan` emulators for the two CAN nodes the 2.0 base controller talks to, plus a pytest
suite. Purpose: exercise CAN1 (ZLAC8015D) and CAN3 (CTL-017) code and the watchdog behaviour
before any wheel or lift moves (P-02, gate G4 preparation). This is not a model of the real
hardware. Where the real behaviour is not recorded, the emulator says so with `TODO(Evans)`.

## Files

| File | What it is |
|---|---|
| `canopen_common.py` | NMT, heartbeat producer, expedited SDO server and a small SDO client |
| `zlac8015d_emulator.py` | One ZLAC8015D V4.x node (node 1): 6040h, 6041h, 6060h, 60FFh, 606Ch, 6083h, 6084h, 605Ah, 6085h, 603Fh, 2000h, 2010h |
| `ctl017_emulator.py` | CTL-017 lift node 0x10: motor model (18 mm/s, 350 mm), Hall counts, three stop sensors, travel timeout, head light and Getac outputs. Object dictionary is a proposal, `TODO(Evans)` |
| `tests/` | pytest suite (36 tests, about 1 minute) |

## Run the tests

```
python3 -m pip install python-can pytest
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
python3 -m pytest tools/can_sil/tests -q
```

The `vcan` fixture creates the interface itself when it is missing (root or passwordless
sudo) and skips cleanly otherwise. Set `CAN_SIL_IFACE` to use another interface name.

## Run an emulator by hand

```
python3 tools/can_sil/zlac8015d_emulator.py --channel vcan0 --node-id 1
python3 tools/can_sil/ctl017_emulator.py --channel vcan0
```

The ZLAC emulator prints its state every second and accepts fault commands on stdin:
`oc-l`, `oc-r`, `uv`, `enc-l`, `enc-r`, `busoff`, `busoff-clear`, `power-cycle`.

## What the emulators model

ZLAC8015D:

- 2000h default 0 keeps the last speed on link loss. A value above 0 stops the wheels when
  no frame addressed to the node arrives for that many ms. Writing 2010h = 1 saves 2000h
  and the ramp times; `power_cycle()` restores them.
- 603Fh reports faults per side (high 16 bits left, low 16 bits right). Any fault disables
  the drive until 6040h = 0x80.
- 6083h and 6084h are read as ms to ramp 0 to 1000 r/min (`TODO(Evans)`: confirm the vendor definition).

CTL-017 (follows D1 to D20 of platform-fw #12; every other detail is `TODO(Evans)`):

- RPDO1 0x210 command, TPDO1 0x190 status (8 bytes, 100 ms and on change), TPDO2 0x290,
  RPDO2 0x310 outputs, EMCY 0x090, heartbeat 0x710. Core heartbeat 0x701 is consumed.
- The node stops with F07 when the command keep-alive or the core heartbeat is missing for
  300 ms during a move or homing (D12 alternative a). Faults are a latched bitmask.
- Stall (no Hall pulses while driving) gives F03, a Hall count that disagrees with a stop
  sensor gives F02, a travel timeout gives F04. Homing goes down to the lower switch (D10).

## Limits

- Not a safety function and not evidence for a safety claim. The hardwired E-stop stays the stop.
- Timing is Python thread timing on a Linux runner. Treat the 200 to 300 ms checks as
  plausibility tests, not as timing measurements. Real timing is measured with `tools/bench`.
- A node restart does not remember the last command. A core that keeps sending the old
  keep-alive after a node restart would start that command again. The core must stop its
  keep-alive when it sees the boot-up message.
