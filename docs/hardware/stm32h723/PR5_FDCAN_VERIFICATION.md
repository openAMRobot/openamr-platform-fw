# PR 5 FDCAN recovery contract, verification and evidence limits

## Scope and source basis

This package implements only the portable FDCAN error/recovery policy in:

- `firmware/transport/include/oamr/fdcan_recovery.h`
- `firmware/transport/src/fdcan_recovery.c`

The source basis is PR 5 in `PR_IMPLEMENTATION_ROADMAP.md`, DESIGN §2.3–2.4 and RT-04–RT-06. It uses the **implemented** PR 3 `oamr/status.h`, `oamr/time.h`, `oamr/bounded_queue.h` and `oamr/can_frame.h` interfaces. The superseded `PR3_PORTABLE_INTERFACES.md` is not an API reference.

This module is C11, HAL-independent and allocation-free. It does not initialize or reset a peripheral, choose STM32 message RAM, generate CubeMX/HAL code, implement a TX/RX driver, decode Daly/CANopen payloads, or change a motion, drive, lift, charging, brake, contactor, watchdog or safety state. Bus-off disables **normal transport TX only**. Successful recovery enables normal transport TX and emits a diagnostic event; it does not restore any permission.

## Exact portable API

The public header is the authoritative signature and calling-contract reference.

| API | Result and required behavior |
|---|---|
| `oamr_fdcan_recovery_init(recovery, event_storage, event_storage_size, event_capacity)` | Caller supplies storage for at least `event_capacity` `oamr_fdcan_recovery_event_t` elements; capacity is elements, not bytes. Successful initialization is ONLINE with empty event storage and zero counters. Null/zero/overflow/insufficient storage arguments return `OAMR_STATUS_INVALID`; a non-null recovery object is left zeroed and offline/uninitialized. |
| `oamr_fdcan2_frame_is_accepted(frame)` | Returns true only for a structurally valid Classical CAN extended data frame with ID `0x18104001` and DLC 8. It does not decode the payload or establish timestamp/freshness validity. |
| `oamr_fdcan_recovery_record_error_status(recovery, timestamp_us, warning, passive, bus_off, tec, rec)` | Updates current warning/passive/bus-off flags and latest TEC/REC, counts false-to-true warning/passive entries and new bus-off episodes, and queues corresponding events. A bus-off while ONLINE immediately gates normal TX and starts the first delay. |
| `oamr_fdcan_recovery_record_tx_failure(recovery, timestamp_us)` | Saturating-counts one adapter-reported TX failure. Does not itself infer bus-off, retry a frame or change transport availability. |
| `oamr_fdcan_recovery_record_rx_overflow(recovery, timestamp_us)` | Saturating-counts one adapter-reported hardware RX FIFO overflow. Does not infer how many frames were lost. |
| `oamr_fdcan_recovery_begin_normal_tx(recovery)` | A TX-submission gate, not a transmitter. ONLINE returns `OAMR_STATUS_OK`; bus-off wait or a pending recovery attempt returns `OAMR_STATUS_ERROR` and counts the rejection. Invalid/uninitialized/corrupt state returns `OAMR_STATUS_INVALID`. Warning/passive alone do not block TX. Recovery adapter operations do not use this normal-TX gate. |
| `oamr_fdcan_recovery_poll(recovery, timestamp_us, out_request)` | Nonblocking, callback-free poll. `out_request` is cleared first. At a due boundary, marks one attempt pending and returns `OK` with `required=true` and its attempt number. Before the deadline, while ONLINE, or while an attempt is already pending, returns `OK` with `required=false`; no attempt is duplicated. |
| `oamr_fdcan_recovery_complete_attempt(recovery, timestamp_us, attempt_result)` | Completes the currently pending attempt. `OAMR_STATUS_OK` returns transport ONLINE, permits normal transport TX, saturating-counts success and queues `RECOVERED`. Every other status is failure: transport remains gated, failure is counted, `FAILED` carries that exact result, and the next delay is selected. Calling without a pending attempt returns `INVALID` without changing state. |
| `oamr_fdcan_recovery_pop_event(recovery, out_event)` | Pops the oldest `oamr_fdcan_recovery_event_t` from the PR 3 bounded queue. `EMPTY` leaves the output untouched. |
| `oamr_fdcan_recovery_get_snapshot(recovery, out_snapshot)` | Copies current state, warning/passive/bus-off status, latest 8-bit TEC/REC, event/cause counters, recovery counters and bounded-event-queue overflow count. Invalid calls zero a non-null output and return `INVALID`. |

Constants are microseconds:

- `OAMR_FDCAN_RECOVERY_FIRST_DELAY_US` = 100000 (100 ms).
- `OAMR_FDCAN_RECOVERY_SECOND_DELAY_US` = 500000 (500 ms).
- `OAMR_FDCAN_RECOVERY_STEADY_DELAY_US` = 1000000 (1000 ms).
- `OAMR_FDCAN2_ACCEPTED_EXTENDED_ID` = `0x18104001`.
- `OAMR_FDCAN2_ACCEPTED_DLC` = 8.

All time arguments are caller-supplied monotonically nondecreasing 64-bit microseconds; timestamp zero is valid and equal timestamps are accepted. A decreasing timestamp returns `INVALID` without applying that operation. Deadlines use subtraction, avoiding addition overflow near `UINT64_MAX`. All cumulative counters saturate at `UINT64_MAX`. Attempt numbers are identifiers, not replay/freshness proof, and can repeat after those counters saturate. TEC/REC are latest hardware-register samples, not cumulative error-frame totals.

### Required scheduling sequence

For bus-off at `t0`, normal TX is rejected immediately. Poll at `t0 + 99,999 us` is not due; poll at `t0 + 100,000 us` is due and reserves attempt 1. Late polling reserves one attempt only; elapsed multiple intervals are not replayed as a burst.

The application performs a bounded, nonblocking adapter-specific recovery action and must eventually call `complete_attempt`, including when the adapter cannot make an attempt. No attempt timeout is invented by this policy. While that attempt is pending, normal TX remains rejected and further polls do not reserve another attempt.

- First attempt failure at `t1`: next attempt is due at `t1 + 500,000 us`.
- Second and every subsequent attempt failure at `tn`: next attempt is due at `tn + 1,000,000 us`.
- Success at `ts`: transport becomes ONLINE immediately and queues `RECOVERED`; a later independent bus-off starts again with 100 ms.
- The 500/1000 ms delay starts at the **failure completion timestamp**, not the preceding poll or adapter-start timestamp.

Repeated bus-off/error reports during an existing recovery episode update current status/TEC/REC but do not restart the schedule, count a new bus-off episode or cancel the pending attempt. Clearing bus-off in hardware status alone never returns transport ONLINE; only an explicit successful attempt does so. A new bus-off report after successful recovery starts a new episode.

Events carry timestamp, type, attempt number (zero for non-attempt events), exact attempt result (`OAMR_STATUS_INVALID` for non-attempt events), and latest TEC/REC. A full event queue rejects the new event and saturating-counts overflow, but cannot prevent error accounting or a transport transition. The affected update returns `OVERFLOW`; consumers can distinguish diagnostics loss from failure to apply recovery state.

All object access requires external serialization across ISR/thread contexts. The queue is not lock-free. The module contains no delay, sleep, dynamic allocation, HAL call or protocol callback. Poll/complete calls are the attempt-result API; a caller must not busy-wait for a deadline.

## FDCAN2 filter and IRQ boundary

The only production acceptance policy implemented here is the documented FDCAN2 policy:

1. Configure one extended-ID exact match for `0x18104001`, routed to RX FIFO0.
2. Globally reject unrelated standard and extended identifiers; do not use a range or mask that accepts neighboring IDs.
3. Reject remote frames, FD/BRS frames, unsupported RTR values and raw DLC encodings that cannot be represented faithfully as PR 3 Classical CAN frames; never truncate DLC greater than 8.
4. After retrieval/conversion, run `oamr_fdcan2_frame_is_accepted` before Daly delivery. Structural validity, exact extended ID and DLC 8 are mandatory. Raw malformed-DLC and rejected-ID counters belong to the adapter's diagnostics; this policy deliberately does not invent count values it did not observe.
5. IRQ/adaptor work is limited to identifying the source, retrieving/copying the frame into an externally serialized bounded queue, recording error/overflow status and signalling application context. There is **no protocol decode, Daly/CANopen decision, dynamic allocation or blocking retry in IRQ context**.

The software helper cannot establish actual STM32 filter registers, global-reject settings or FIFO routing. Those require mock-HAL register/call tests and later target inspection/bench evidence. A frame accepted by this helper is not automatically fresh, healthy or a valid Daly response; payload decoding is PR 11.

## FDCAN1 and FDCAN3 approval restriction

Exact FDCAN1/FDCAN3 standard CANopen acceptance-filter lists are unspecified in the reviewed baseline. This package does **not** invent IDs, masks, ranges, FIFO destinations or “accept all for development” behavior.

Until separately reviewed and approved:

- FDCAN1/FDCAN3 production acceptance filters and normal protocol traffic are not enabled by this module.
- An adapter integration must keep a clearly documented read-only baseline (no normal TX and no production protocol consumption), or configure standard and extended global rejection with no acceptance filter. Either scoped choice must be explicitly named in that adapter's configuration and verification; read-only must not be represented as production CANopen support.
- FDCAN1 traction and FDCAN3 lift commands, CANopen object/PDO semantics and automatic motion/lift recovery remain outside this package.

The nominal design assignments (FDCAN1/3 at 500 kbit/s and FDCAN2 at 250 kbit/s, SJW=1 TQ) and message-RAM offsets 0/256/512 words are integration requirements, not values verified or configured by these two recovery files.

## Host/mock tests required

Add a dedicated CTest host target compiling `fdcan_recovery.c` with PR 3 `bounded_queue.c` and `time.c`, with strict C11 warnings. Run the same suite under ASan/UBSan and separately without sanitizers. At minimum, include the following named cases and assertions:

1. **Initialization and invalid state:** null pointers, zero capacity/storage/element extent, multiplication overflow and one-byte-short storage; failed initialization cannot permit TX; zero timestamp is valid; snapshot invalid output is zeroed.
2. **FDCAN2 exact filter:** accept extended `0x18104001`, DLC 8; reject standard same numeric ID, extended neighbors `0x18104000`/`0x18104002`, unrelated standard/extended IDs, DLC 0/7 and raw/converted DLC 9, invalid ID ranges/types and null. Include a nonzero-error-flag frame test proving the helper does not silently classify it as a healthy protocol response. Adapter tests separately prove remote/FD rejection and exact FIFO0/global-reject register programming; no protocol bytes are interpreted in the filter test.
3. **Warning/passive/bus-off:** first assertion counts and queues one entry each; repeated true and continued false do not repeat entry events; clear then reassert counts again; independent TX-failure and RX-overflow counters; latest TEC/REC including 0/255; `UINT64_MAX` saturation fixtures for every cumulative counter and queue overflow.
4. **Exact first deadline:** bus-off at time zero immediately rejects normal TX; polls at 99,999 us make no request, 100,000 us makes exactly request 1; repeated poll while pending does not increment attempts. A late poll makes one request, not a catch-up burst.
5. **Failed-attempt schedule:** complete attempt 1 with each representative non-OK status; verify `FAILED` preserves that status and next request is absent at +499,999 us and present at +500,000 us. Fail attempt 2 and verify +999,999 us absent/+1,000,000 us present; repeat for later attempts. No internal callback or retry occurs.
6. **Attempt result and completion:** success returns ONLINE, allows normal TX and emits one `RECOVERED` event carrying the right attempt number. Double completion, completion without poll, completion while ONLINE/waiting, null request/output and decreasing timestamps return `INVALID` without unauthorized state changes. Pending attempts continue to reject TX.
7. **Bus-off repetition and new episodes:** repeated status reports during wait/pending do not reset deadlines or add attempts/bus-off events. `bus_off=false` without successful completion does not restore TX. Successful recovery followed by another bus-off restarts at 100 ms and increments cumulative bus-off count.
8. **64-bit timing:** equal timestamps, regression preservation, bus-off/poll/complete near `UINT64_MAX`, and elapsed deadlines above 32 bits. Verify no wrapped deadline can cause an early attempt.
9. **Event queue bounds:** capacity-one overflow drops the newest event without changing warning/bus-off/recovery state, increments `event_overflows`, returns `OVERFLOW`, and preserves FIFO order of retained events; draining then pushing works. Pop-empty/invalid leaves output untouched. No allocation or unbounded diagnostic retry occurs.
10. **Transport-only proof:** a test-owned motion/safety sentinel is unchanged by warning, passive, bus-off, failed attempts and successful recovery. Normal-TX gate assertions are transport assertions only. Intentionally broken mock/fake-clock checks confirm the harness can fail.
11. **Adapter/queue integration (separate owning PR 5 files):** mock-HAL message-RAM bounds/no overlap, init and notification/start order, exact FDCAN2 registers/FIFO0/global rejection, bounded RX full/empty/wrap/burst, malformed DLC/unrelated IDs, hardware and queue overflow independence, TX submission/completion/failure/would-block and no blocking wait. FDCAN1/3 tests must assert the explicitly approved rejection/read-only baseline rather than invent production IDs.

Acceptance for this module requires all listed portable cases to pass in Debug and Release host configurations, no sanitizer diagnostics, and inspection showing no dynamic allocation, blocking delay or protocol decoder. The full PR additionally requires both STM32 builds, map/static analysis and generated callback-link checks from the adapter owners.

## Board integration reconciliation and remaining work

The board adapter uses corrected message-RAM offsets 0/512/1024 words after the historical 0/256/512 layout was found to overlap with 16 RX FIFO elements plus eight TX elements. Monitor-only FDCAN1/3 have zero TX allocation; FDCAN2 retains eight Daly TX elements. This is a source/HAL-derived correction requiring design-owner review, not an approved design amendment.

The portable recovery policy is integrated into the adapter: Daly TX is gated by transport availability, error snapshots feed the policy, and a due attempt performs a bounded stop/restart of only the affected FDCAN instance. No safety, actuator or motion state is changed. Startup initialization failure is retained in diagnostics rather than converted into an operational CAN claim.

Static `tools/ci/check_fdcan_config.py` validates current timing/filter/capacity and message-RAM extents. The adapter's recovery tests and physical-bus validation remain separate evidence requirements. FDCAN1/3 broad standard reception in bus-monitoring mode is diagnostic-only, not production CANopen support.

## Bench limits and outstanding Level 4 work

**No physical FDCAN, transceiver, BMS, motor controller or lift controller verification has been performed.** Host/mock tests prove deterministic software policy only; they do not prove STM32 register values, bit timing, electrical operation, recovery of a real bus, real error counters, safe stopping or interoperability.

Supervised bench work must independently record:

- FDCAN1/3 nominal 500 kbit/s and FDCAN2 250 kbit/s, measured sample point and separate approved SJW=1 versus SJW=2 comparison; no silent timing substitution.
- Exact FDCAN2 extended filter/FIFO0/global rejection, DLC 8 enforcement and rejection of unrelated IDs, malformed/remote/FD frames; actual FDCAN1/3 filters only after approved lists exist.
- Controlled warning/passive/bus-off injection with timestamped TEC/REC/status captures, TX cessation at bus-off, observed 100/500/1000 ms scheduler behavior, explicit attempt results and recovery event without motion/safety permission restoration.
- TX failure, hardware RX FIFO overflow and application queue overflow independently, including sustained representative traffic and no IRQ decode/allocation/blocking.
- Transceiver supply, logic levels, topology, common ground, termination and isolation for each connected device; approved FDCAN2/Daly wiring and delivered protocol variant.
- Disconnect/reconnect and real bus-off recovery on a supervised bench, with all actuator/safety tests separately authorized. Wheels-up tests, charging behavior and physical safe-state acceptance are not implied by this document or by host-test success.

Until those records exist, actual communication, timing margins, filter behavior on hardware and physical bus-off recovery remain **BENCH GATED**.
