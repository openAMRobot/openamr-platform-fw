# 04 CAN2 to the Daly BMS

**Proves:** CAN2 (FDCAN2, PB12 RX and PB6 TX, through the isolated CAN module) runs at 250 kbit/s,
the Daly BMS answers the request frames, the decoded pack values match a multimeter, and the
status goes stale when the BMS is silent.
**Moves anything?** No. This is a measurement bench. **No robot harness, no K1/K2, no charger, no
load on the pack.** A pack or a BMS on its own with its balance leads is the maximum.

> **Protocol status: unconfirmed.** The draft adapter (`firmware/battery_monitor`, platform-fw #40)
> uses the older Daly CAN V1.0 request and response frames: 29-bit identifier `0x18 | DataID |
> destination | source`, host `0x40`, BMS `0x01`, Data IDs `0x90` to `0x93` and `0x98`, 8 data bytes,
> big endian. **Newer Daly M-series firmware may speak a different protocol on CAN.** This
> procedure's first job is to find out which one the delivered unit speaks. The order code of the
> delivered BMS: `TODO(Arka)`.

## Preconditions

- A Daly BMS on a bench pack or a bench cell set. Which one is `TODO(Arka)`. If only a bare BMS
  with no cells is available, only steps 1 to 3 are possible; say so in the record.
- The BMS CAN output goes to the isolated CAN module (CTL-014; the isolator click MIKROE-5785 is
  wired straight to PB12 and PB6 with jumpers, no click shield, P-03 item 26). Module power: 3.3 V or 5 V as its
  datasheet says: `TODO(Evans)`. For the PC-only steps (1 to 4) the PC adapter can be connected
  to the BMS directly, **without the NUCLEO**; for the NUCLEO steps (5, 6) the PC adapter is
  disconnected. One master at a time.
- BMS CAN wiring and termination: the BMS end of the bus has its own termination or not,
  `TODO(Evans)`. Measure with `tools/bench/termination_check.md` (power off): about 60 ohm with
  two terminations. CAN2 has the BMS and the isolator module at its two ends.
- A multimeter for the pack voltage at the BMS terminals. Fire precautions for a lithium pack on
  the bench (LiFePO4 cells, EVE LF105 in the robot): pack on a non-flammable surface, nobody
  leaves the room with the pack connected, fire extinguisher location known.
- Charger: not connected in this procedure.

## Step 1. Termination, power off

Follow `tools/bench/termination_check.md` for CAN2. Pass: 55 to 65 ohm, no short to ground.

## Step 2. Interface at 250 kbit/s

```
sudo tools/bench/can_up.sh can2 250000
ip -details link show can2
```

Expected: `state UP`, `bitrate 250000`, `restart-ms 0`, `qlen 1000`; exit 0. Note the different bit rate from CAN1 and CAN3.

## Step 3. Listen, then ask

The BMS may send nothing until asked. First listen:

```
candump -L can2 > $(date +%F)-can2-listen.log &
sleep 10; kill %1
```

Record what, if anything, is on the bus. Then ask for the pack data (Data ID 0x90) once, by hand,
with an extended identifier `0x18900140` (priority/addr `0x18`, Data ID `0x90`, destination BMS
`0x01`, source host `0x40`), 8 zero data bytes:

```
candump -L can2 &
cansend can2 18900140#0000000000000000
sleep 1; kill %1
```

Expected from the V1.0 protocol: a response with identifier `18904001` and 8 data bytes (destination host `0x40` and source BMS `0x01`
swapped). Decode: bytes 0 to 1 pack voltage in 0.1 V, bytes 4 to 5 current as (raw minus 30000) in 0.1 A,
bytes 6 to 7 state of charge in 0.1 percent (all big endian).
Example arithmetic: `0x0A28` is 2600, which is 260.0 V; check the unit against the pack (the robot
pack is 8 cells in series, about 25.6 V nominal, so a raw value near `0x0100` or 256 is plausible).

PASS: a response with the expected identifier arrives. **If nothing answers or the identifier
differs, the protocol is not V1.0: stop the NUCLEO steps, record the raw frames with
`candump -L`, and send them to Evans.** That is a valid and valuable result.

Repeat for the other Data IDs the adapter uses:

| Data ID | Request | Payload content |
|---|---|---|
| 0x90 | `18900140#0000000000000000` | pack voltage, current, SOC |
| 0x91 | `18910140#0000000000000000` | max and min cell mV |
| 0x92 | `18920140#0000000000000000` | max and min temperature (raw minus 40, degC) |
| 0x93 | `18930140#0000000000000000` | state, charge MOS, discharge MOS, remaining mAh |
| 0x98 | `18980140#0000000000000000` | failure bits |

Request IDs were checked with `cansend` and `candump` on a `vcan` interface (the frame leaves as
`18900140#...`); the responses were not, there is no Daly emulator in this repository.

## Step 4. Values against the meter

Measure the pack voltage at the BMS terminals with the multimeter while reading Data ID 0x90.
Pass: the decoded pack voltage is within `TODO(Evans)` of the meter (suggest 0.5 percent as a
starting point for the discussion). Cell voltages (0x91): the max and min cell mV against the
meter on the balance leads, within `TODO(Evans)`. Temperature (0x92): against a thermometer, within 3 degC
(placeholder, `TODO(Evans)`). Record all of them. The sign convention of the current must be tested
only with a load connected and is `TODO(Evans)`: do not connect a load this week.

Failure bits (0x98): all zero on a healthy bench pack. Do not trigger a fault deliberately on a lithium pack.
The firmware's draft fault policy is: any level-2 alarm bit pattern `0xAA` in bytes 0 to 3, or any bit in bytes
4 to 6, is a fault; any level-1 pattern `0x55` is a warning (proposal, `TODO(Evans)`).

## Step 5. The NUCLEO asks, the PC listens **[firmware gap for the status path]**

Disconnect the PC adapter from the BMS side of the bus **as a master**; keep it connected as a
listener only if it has a listen-only mode (`ip link set can2 type can bitrate 250000 listen-only on`,
`TODO(Arka)`: whether the adapter supports it). Otherwise connect the PC to the same bus as a passive tap
that does not acknowledge, or skip to the debugger.

In the draft, the control tick sends one request per tick period of 40 ms in turn (five Data IDs, so each
about 5 Hz, the 5 Hz minimum of the I8 contract) once the CAN2 health state is usable.

```
candump -L can2
```

Expected: request frames `18900140`, `18910140`, `18920140`, `18930140`, `18980140` in turn, about 25
frames per second in total, and the BMS responses. With the debugger (procedure 01, step 5) read the
adapter state. `s_daly` is file-static in `Port/Src/port_app.c`, with the status in `s_daly.status`
(variable names are those of #40; confirm in the image):

```
(gdb) print s_daly.status
(gdb) print s_daly.frames_rx
(gdb) print s_daly.tx_failures
```

Expected: `have_data` 1, voltages equal to step 4, `tx_failures` 0, `frames_rx` rising.
**[firmware gap]** The BMS status is not yet published to the host in the draft (the micro-ROS side
is absent). The host value of the status will be checked in procedure 02 once it is.

## Step 6. Stale and recovery

1. With the NUCLEO asking (step 5), disconnect the CAN2 cable at the BMS end (or the BMS supply
   switch). Wait 5 s.
2. Expected: `oamr_daly_fresh` goes false after the stale time (draft 3 s, `TODO(Evans)`, P-02
   rev19.2). With the debugger: `print s_daly.status.have_data` stays, but freshness is computed
   on read; the safety core's BMS input (`bms_rx_us`, reason `OAMR_SC_R_BMS_FAULT` bit 7, 0x80)
   shows the BMS as stale. **The BMS must never be inferred healthy from old data.**
3. Reconnect. Expected: data fresh again within one cycle.
4. Count `tx_failures` and the CAN2 bus-off state. Pulling the cable can cause bus-off on the
   NUCLEO side (no acknowledgement): the draft restarts with a hold-off and latches after
   repeated events (`can_health`). Record how many events the pull produced.

## Pass criteria

- The protocol is identified (V1.0 or something else, with the raw frames saved).
- If V1.0: all five Data IDs answer, values within the limits against the meter, 250 kbit/s clean.
- Stale detection after the stale time, recovery after reconnect.

## Stop condition

Any heating, swelling, smell or voltage mismatch above 1 percent: disconnect the pack, record, stop.
A pack that is imbalanced beyond the BMS limits: do not charge it on the bench in this procedure.

## Record

BMS order code and firmware, adapter, the raw frames of step 3, decoded values and meter readings,
the stale time measured, `candump -L` logs.

## Open values (TODO)

- `TODO(Evans)` Daly protocol for the delivered order code, the tolerances against the meter, the stale time (draft 3 s), isolator module supply, termination placement.
- `TODO(Arka)` BMS order code and firmware, bench pack, adapter listen-only support.
