# CAN1 drive link: ZLAC8015D V4.2 on the STM32H723 base controller

Status: proposal for the firmware owner (Evans). First reviewer: Mohamed.
Nothing in this document is measured on hardware. Every number that is not a
CiA standard value or a decision cited by ID is marked as an assumption or a
`TODO(Evans)` parameter.

Scope: CAN1 (FDCAN1, 500 kbit/s, one ZLAC8015D V4.2 as CANopen node 1). The
code in `firmware/canopen` and `firmware/drive_telemetry` covers the
read-only part only: SDO upload codec, heartbeat, NMT and EMCY decode, a
heartbeat consumer that reports, an upload scheduler that can only read, a
telemetry decoder and a configuration read-back comparator. Writing targets,
enabling the drive and reacting to a lost link are the owner's code
(platform-fw #47). Sections 3 and 4 below give him numbers and test vectors
for that code; section 5 is "specification, not code".

Sources: CiA 301 and CiA 402 for frame layout; decision D-24 (platform-fw #17
and #38 threads) for the proposed drive values; the vendor CANopen routine
V1.07 (labelled V4.0) for object numbers; owner review of platform-fw #46 for
the SDO timing finding. The V4.2 EDS and manual are not in hand (section 7).

## 1. What the read-only pieces give you

| Module | File | Does | Never does |
| --- | --- | --- | --- |
| COB-ID helpers | `firmware/canopen/include/oamr/co_cobid.h` | predefined connection set, classify an id | remap anything |
| SDO codec | `co_sdo.h` | encode upload request, decode expedited upload response and abort; a download encoder exists as a tested codec function | get called for a write by anything in this package (a ctest guard checks this) |
| NMT, heartbeat, EMCY | `co_nmt_emcy.h` | decode heartbeat and boot-up, decode NMT commands seen on the bus, decode EMCY | encode an NMT command |
| Heartbeat consumer | `co_hb_consumer.h` | last-seen time, last NMT state, boot-ups, malformed count, WAITING / ALIVE / LOST against a caller timeout with no default | act on LOST |
| Upload scheduler | `co_upload_sched.h` | cycles a caller list, one expedited upload at a time, timeout and retry counters, a sample per entry with its own validity and sequence number | send anything but `40h` initiate-upload requests |
| Telemetry decode | `firmware/drive_telemetry/include/oamr/zlac_telemetry.h` | 606Ch:01 and 606Ch:02 each with its own validity; 603Fh as raw halves a (bits 31..16) and b (bits 15..0); 6041h CiA 402 state and bits; 2026h:05 confirmation | give 603Fh halves a left or right meaning; convert speed when 2026h:05 is not confirmed |
| Read-back comparator | `config_readback.h` | compare caller-supplied expected values with read values, report MATCH, MISMATCH, SIZE_MISMATCH, UNVERIFIED | write, raise a fault, or treat an unread value as a match |

Status of this code: SOURCE COMPLETE, HOST-TESTED (gcc and clang, ASan and
UBSan), CROSS-COMPILED (arm-none-eabi-gcc, cortex-m7, hard float). Not run on
a board.

## 2. Frame timing at 500 kbit/s

Classic CAN, 11-bit identifier, n data bytes. Bits on the wire without
stuffing: SOF 1 + identifier 11 + RTR 1 + IDE 1 + r0 1 + DLC 4 + data 8n +
CRC 15 + CRC delimiter 1 + ACK 2 + EOF 7 + interframe space 3 = 47 + 8n.
Stuffing applies to the 34 + 8n bits from SOF to the end of the CRC; the worst
case adds floor((34 + 8n - 1) / 4) bits.

| Frame | n | Bits, no stuffing | Bits, worst case | Time at 2 us/bit, worst case |
| --- | --- | --- | --- | --- |
| SDO request or response, PDO with 8 bytes | 8 | 111 | 111 + 24 = 135 | 270 us |
| Heartbeat | 1 | 55 | 55 + 10 = 65 | 130 us |
| NMT command | 2 | 63 | 63 + 12 = 75 | 150 us |

An SDO transfer is two 8-byte frames: 540 us of bus time at worst, plus the
drive's SDO response time `t_srv` (TODO(Evans): measure from a capture; not
in any document we hold) plus the controller's own turnaround.

Assumption for the arithmetic: a control cycle of 20 ms (50 Hz, as the
current loop documentation). Change the cycle and the percentages scale.

## 3. Wheel targets: RPDO, 60FFh:3 or two SDO writes

The owner's review of platform-fw #46 found that with one SDO transfer per
20 ms tick and six transfers per cycle (two target writes, four reads), a
target reaches the drive up to 120 ms late and the second wheel one tick
(20 ms) after the first. The owner must choose a transport. The table puts the
three options side by side. Bus load counts target frames only; telemetry is
in section 3.4.

### 3.1 Option A: one RPDO with both targets

One 8-byte RPDO (two I32 values) per cycle, no response (PDOs are not
confirmed).

- Bus load: 270 us per 20 ms = 1.35 %.
- Latency: one frame (270 us) plus at most one frame already on the bus
  (270 us) plus the controller queueing time, so about 0.54 ms worst case from
  "queued" to "received" when the bus is not saturated. Not dependent on
  `t_srv`.
- Left-right skew on the bus: 0 (both values in one frame). Skew inside the
  drive: unknown.
- Costs: the RPDO mapping has to be configured (1400h / 1600h on the drive),
  which is itself a set of SDO writes in the owner's configuration step and
  must be read back. No delivery confirmation; delivery evidence comes from
  606Ch and from the drive's link-loss timer staying quiet.
- Open: whether V4.2 lets 60FFh:01 and 60FFh:02 be mapped into one RPDO, how
  many RPDOs it has, and whether it supports RPDO event-timer (deadline)
  monitoring (section 7, Q4 to Q6).

### 3.2 Option B: 60FFh:3 (synchronous target, one 32-bit value)

The vendor routine describes a synchronous mode (200Fh = 1) in which 60FFh:3
carries both targets as two 16-bit halves. Sent by SDO it is one transfer per
cycle; it can also be mapped into an RPDO (then it behaves as option A with
4 data bytes).

- Bus load by SDO: 2 x 270 us = 540 us per 20 ms = 2.7 %.
- Latency by SDO: request frame plus queueing, about 0.54 ms worst case to
  the drive, as option A; the confirmation arrives after a further
  `t_srv` + 270 us.
- Skew on the bus: 0 (one value).
- Costs: targets limited to 16 bits each (vendor range of 60FFh is
  -1000 to 1000 r/min, so this fits); the half order is documented only in the
  vendor routine and has to be confirmed on the bench, like 603Fh; 200Fh = 1
  is another configuration value to write, read back and save.

### 3.3 Option C: two SDO writes, 60FFh:1 then 60FFh:2

Two confirmed transfers per cycle.

- Bus load: 4 x 270 us = 1.08 ms per 20 ms = 5.4 %.
- Latency for the first wheel: about 0.54 ms worst case, as above.
- Skew between the wheels when both writes go out in the same cycle back to
  back: the second request can leave only after the first response, so skew
  is at least `t_srv` + 2 frames = `t_srv` + 0.54 ms worst case, plus the
  controller's turnaround.
- Skew with one transfer per tick (the #46 pattern): one full tick, 20 ms.
  At 0.5 m/s that is 10 mm of travel with one wheel on the old target.
- Latency with one transfer per tick and k transfers per cycle: up to
  k x 20 ms. With k = 6 (two writes, four reads) that is 120 ms, the number in
  the owner's finding.

### 3.4 Telemetry reads (what this package's scheduler does)

The scheduler reads one object at a time. With N objects in the list, tick
period T and SDO round trip r = 540 us + `t_srv`, each object is refreshed at
best every N x max(T, r).

| List | N | Refresh per object at T = 20 ms | Bus time per cycle of N | Load if run back to back every 20 ms |
| --- | --- | --- | --- | --- |
| 606Ch:01, 606Ch:02 | 2 | 40 ms | 1.08 ms | 5.4 % |
| plus 6041h, 603Fh | 4 | 80 ms | 2.16 ms | 10.8 % |

The two velocity channels are read 20 ms apart with this pattern, so a
"both wheels" sample is two samples up to one tick apart. If the owner needs
both channels from the same instant, the drive's TPDO with 606Ch:01 and
606Ch:02 in one frame is the better source; the decoder in
`zlac_telemetry.h` takes samples, so a TPDO source can fill the same
`oamr_co_sample_t` fields.

Heartbeat at the proposed 100 ms (D-24): 130 us per 100 ms = 0.13 %.

### 3.5 Summary

| Option | Bus load (targets) | Latency to drive, worst case, idle bus | Left-right skew | Delivery confirmed |
| --- | --- | --- | --- | --- |
| A, RPDO with both targets | 1.35 % | about 0.54 ms | 0 on the bus | no |
| B, 60FFh:3 by SDO | 2.7 % | about 0.54 ms | 0 on the bus | yes |
| C, two SDO writes, back to back | 5.4 % | about 0.54 ms (first wheel) | `t_srv` + 0.54 ms or more | yes |
| C, one transfer per 20 ms tick (#46) | 5.4 % | up to k x 20 ms (120 ms for k = 6) | 20 ms | yes |

All options together with four telemetry reads stay under 20 % of the bus at
50 Hz. The decision is about latency and skew, not about bandwidth.

## 4. Five supervision mechanisms side by side

These are different mechanisms with different numbers. None of them is a
stopping time, and none of them is a safety function: the Category 0 stop is
the hardwired chain (PNOZ s3, K1, K2). Values are D-24 or I8 proposals unless
marked frozen.

| # | Mechanism | Where it runs | What it watches | Proposed value | What it does when it fires | What it does not cover |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Command freshness | base controller | age of the last valid velocity command from the host | 200 ms (I8 decision 3, open for the STM32) | owner's code: commands zero targets | a hung or reset STM32 (the check runs on it); a CAN1 fault between STM32 and drive; a drive that ignores the zero target |
| 2 | CAN loss seen by the controller | base controller | drive heartbeat (consumer), SDO timeouts, bus-off | consumer 300 ms (D-24) for node 1 | owner's code: reports the drive lost, inhibits | it cannot stop the drive (the bus is the thing that failed); a drive that is alive but misconfigured; a hung STM32 |
| 3 | Drive link-loss, 2000h | ZLAC8015D | time since the drive last received communication | 250 ms (D-24), factory value 0 = off | drive reacts on its own (ramp, freewheel or fault: not known) | a controller that is alive but keeps sending a wrong or stale target (link looks healthy); any time before commissioning writes and saves the value; what counts as "communication" (section 7, Q2) |
| 4 | Drive ramp | ZLAC8015D | deceleration profile 6084h, quick stop 6085h | TODO(Evans): values for 1.5 m/s2 travel and 1.0 m/s2 hold-and-move (P-03 item 30 A6) | shapes how speed goes to zero once a zero target or a stop arrives | detection of anything; a power cut (no ramp then); whether the 2000h reaction uses this ramp |
| 5 | Host status freshness | host | receipt age of `/base/status` and `/base/aux_status` | 0.5 s and 0.6 s (frozen, I8) | host marks the base STALE, blocks Resume | the wheels: it is a host view only and stops nothing on CAN1 |

Gaps that only a combination closes:

- STM32 hangs while driving: 1 and 2 cannot run; only 3 stops the drive,
  after up to 250 ms plus the drive's reaction, then 5 tells the host after
  0.5 s. Until 2000h is written and saved, nothing in software stops the
  wheels in this case.
- CAN1 cable cut: 2 sees it after the consumer time; 3 sees it on the drive
  side. Both wheels have to be observed separately in the bench test (D-24).
- Host stops sending: 1 fires; 2, 3 and 5 stay quiet if the controller keeps
  the bus alive with zero targets. That is the intended path.
- Controller alive, command logic stuck sending an old non-zero target: 3
  never fires because the link is active. Only 1, implemented so that it
  cannot be bypassed by the stuck logic, covers this. This is an owner design
  point for platform-fw #47.

Vendor rule quoted in the routine: the consumer time must be greater than
twice the producer time. With D-24: 2 x 100 ms = 200 ms < 300 ms. Holds, if
1017h is in ms on V4.2 (to settle by a capture, D-24).

## 5. Enable sequence and D-24 configuration as test vectors

**Specification, not code.** These vectors are for the owner's
implementation and for a bench capture to compare against. Bytes are hex,
node 1, request id 601h, response id 581h, NMT id 000h. Sizes marked "?" are
not documented for V4.2; the vector shows the U16 encoding that platform-fw
#46 assumed, and the owner must confirm it from the EDS before use.

### 5.1 Boot: read and compare (no write)

| Step | Frame sent | Expected answer | Pass rule | On failure |
| --- | --- | --- | --- | --- |
| R1 | none | 701 `00` (boot-up) then 701 `7F` | heartbeat seen within the consumer time | do not proceed; report drive absent |
| R2 | 601 `40 26 20 05 00 00 00 00` | 581 `4F 26 20 05 01 ..` (size ?) | value equals the required resolution (D-24: 1) | no speed maths; report |
| R3 | 601 `40 17 10 00 00 00 00 00` | 581 `4B 17 10 00 64 00 ..` | 1017h = 100 (D-24, unit to settle) | DRIVE_CONFIG_MISMATCH (proposed) |
| R4 | 601 `40 00 20 00 00 00 00 00` | 581 `4B 00 20 00 FA 00 ..` (size ?) | 2000h = 250 (D-24) | DRIVE_CONFIG_MISMATCH; a read of 0 means link-loss is off |
| R5 | 601 `40 60 60 00 00 00 00 00` | 581 `4F 60 60 00 03 ..` | 6060h = 3, profile velocity | DRIVE_CONFIG_MISMATCH |
| R6 | 601 `40 3F 60 00 00 00 00 00` | 581 `43 3F 60 00 00 00 00 00` | no fault in either half | report halves a and b raw |
| R7 | 601 `40 41 60 00 00 00 00 00` | 581 `4B 41 60 00 xx xx ..` | state SWITCH_ON_DISABLED or READY_TO_SWITCH_ON | do not enable |

R2 to R7 are what `oamr_co_upl_*` and `oamr_rb_compare` do today with the
expected values passed in by the caller. Any abort or timeout makes the entry
UNVERIFIED, never a match.

### 5.2 Commissioning only: write, read back, test, then save

Never at boot (D-24). Each write is followed by a read-back of the same
object.

| Step | Frame sent | Expected answer | Then |
| --- | --- | --- | --- |
| C1 | 000 `80 01` (enter pre-operational) | 701 `7F` | continue |
| C2 | 601 `2B 17 10 00 64 00 00 00` (1017h = 100) | 581 `60 17 10 00 00 00 00 00` | read back as R3 |
| C3 | 601 `2B 00 20 00 FA 00 00 00` (2000h = 250, size ?) | 581 `60 00 20 00 00 00 00 00` | read back as R4 |
| C4 | CAN1 loss test with both wheels observed separately, wheels off the ground | each wheel stops; record ramp, freewheel or fault, stop time and distance per wheel | only on pass go to C5 |
| C5 | 601 `2B 10 20 00 01 00 00 00` (2010h = 1, size ?) | 581 `60 10 20 00 ..` | power-cycle the drive |
| C6 | read R1 to R7 again after the power cycle | all match | commissioning record complete |

Abort answer to any write, for example 581 `80 00 20 00 30 00 09 06`
(value range exceeded): stop, do not save, record the code.

### 5.3 Enable (owner code; vectors for his tests)

Precondition: R1 to R7 pass, the base controller's own readiness allows
motion (owner's state machine), both targets zero.

| Step | Frame sent | Expected answer |
| --- | --- | --- |
| E1 | 601 `23 FF 60 01 00 00 00 00` | 581 `60 FF 60 01 00 00 00 00` |
| E2 | 601 `23 FF 60 02 00 00 00 00` | 581 `60 FF 60 02 00 00 00 00` |
| E3 | 000 `01 01` (NMT start node 1) | 701 `05` within one heartbeat period |
| E4 | 601 `2B 40 60 00 06 00 00 00` | 581 `60 40 60 00 ..`; 6041h & 006Fh = 0021h |
| E5 | 601 `2B 40 60 00 07 00 00 00` | ack; 6041h & 006Fh = 0023h |
| E6 | 601 `2B 40 60 00 0F 00 00 00` | ack; 6041h & 006Fh = 0027h |

Failure vectors for the owner's tests:

| Injected | Expected behaviour of the owner's code |
| --- | --- |
| no answer to E4, E5 or E6 within the SDO timeout | stays disabled, reports, no retry loop that keeps the drive half enabled |
| 6041h shows FAULT (bit 3) after E6 | stays disabled; 603Fh halves reported raw; no automatic fault reset |
| heartbeat 701 `00` (boot-up) while enabled | drive restarted: treat as lost; full boot read (5.1) before any re-enable |
| 606Ch:01 valid and 606Ch:02 aborted | the decoder reports channel 2 READ_FAILED and channel 1 VALID; "both wheels stopped" cannot be claimed |
| 2026h:05 reads other than required | velocity decode stays UNCONFIRMED; no speed maths |

## 6. Using the read-only pieces

```c
static const oamr_co_object_t objs[] = {
    {0x2026, 0x05}, {0x6041, 0x00}, {0x606C, 0x01}, {0x606C, 0x02}, {0x603F, 0x00},
};
oamr_co_upl_config_t cfg = {
    .node_id = 1, .objects = objs, .n_objects = 5,
    .timeout_us = SDO_TIMEOUT_US,          /* TODO(Evans) */
    .max_retries = SDO_RETRIES, .max_retries_set = true,   /* TODO(Evans) */
    .send = port_can1_send, .send_ctx = &can1,
};
oamr_co_upl_init(&sched, &cfg);
/* every tick */      oamr_co_upl_tick(&sched, now_us);
/* every CAN1 frame */ oamr_co_upl_on_frame(&sched, &frame, now_us);
                       oamr_co_hb_on_frame(&hb, &frame, now_us);
```

No parameter has a default. A missing one leaves the module in
NOT_CONFIGURED and it sends nothing.

## 7. Open questions for the V4.2 EDS and manual

1. Data type and size of 2000h, 2010h and 2026h:05. platform-fw #46 assumed
   U16 for 2000h and 2010h; nothing confirms it.
2. What restarts the 2000h timer: any frame on the bus, any frame to node 1,
   only SDO, only RPDO, or SYNC?
3. What the drive does when 2000h expires: ramp with 6084h, quick stop with
   6085h, freewheel, or fault, and whether that is per wheel.
4. Number of RPDOs and TPDOs, and which objects are mappable (60FFh:01,
   60FFh:02, 60FFh:03, 606Ch:01, 606Ch:02, 6041h, 603Fh).
5. Whether RPDO event-timer (deadline) monitoring is supported.
6. Whether the drive consumes a heartbeat (1016h) from the controller.
7. Unit of 1017h on V4.2 (D-24 asks to settle it by capture).
8. Which half of 603Fh belongs to which motor, and the Hall fault code: the
   Quick Start V1.01 contradicts itself; the code keeps halves a and b raw.
9. The meaning of each 2026h:05 value and the unit of 606Ch for each.
10. Half order of 60FFh:03 in synchronous mode (200Fh = 1).
11. EMCY codes the drive sends, and whether it sends EMCY at all.
12. Whether 2010h = 1 saves 1017h and 2000h together with the drive
    parameters.
13. Behaviour on NMT stop and NMT pre-operational while enabled.
14. SDO server response time `t_srv` (to measure; needed for section 3).
15. Whether the drive sends TPDOs by default after NMT start, and at what
    rate (bus load, section 3.4).
16. Statusword bits the drive actually sets beyond the CiA 402 state bits
    (remote, target reached, internal limit).
