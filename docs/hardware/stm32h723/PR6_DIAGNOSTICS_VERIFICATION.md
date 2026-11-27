# PR 6 portable diagnostics contract, acceptance and evidence limits

## Scope and source basis

The portable diagnostic component contains these six implementation files:

- `firmware/diagnostics/include/oamr/reset_cause.h` and `src/reset_cause.c`
- `firmware/diagnostics/include/oamr/fault_record.h` and `src/fault_record.c`
- `firmware/diagnostics/include/oamr/diagnostic_tx.h` and `src/diagnostic_tx.c`

The source basis is PR 6 in `PR_IMPLEMENTATION_ROADMAP.md`, DESIGN §5.1–5.2, §7.4 diagnostic subset, §8.2 and §10; RT-17, RT-19 and RT-26–RT-27. PR 5 supplies the implemented PR 3 `oamr/status.h` and `oamr/transport.h` conventions used here. This is a proposed review schema, not an approved persistence format or board integration.

These six portable files are C11, HAL-independent and allocation-free. They contain no hardware register accesses, RCC layouts, delays, polled writes, callbacks from formatters, dynamically sized objects, watchdog operations, command timeout policy, actuator outputs, motion/lift/charging/brake/contactor control or safety permissions. The USART3 PD8/PD9 115200 8N1 adapter and CubeMX configuration are integrated separately and covered below. Fault handlers and a linker retention section remain unimplemented.

## Reset-cause contract

`oamr_reset_cause_decode(raw_flags, definitions, out_cause)` accepts a raw 32-bit value plus an explicit `oamr_reset_flag_definitions_t`. The board adapter must obtain that raw value **before clearing any RCC flags** and provide masks in that same raw layout. No STM32H7 bit position is hardcoded by the portable module.

Each mask independently selects `POWER_ON`, `PIN`, `SOFTWARE`, `IWDG`, `WWDG`, `LOW_POWER`, `BOR`, or board-defined `OTHER`. Every matching bit is retained, so simultaneous causes are not reduced to a priority winner. A bit may deliberately appear in multiple adapter masks. Raw bits absent from all eight masks appear in `unknown_flags` and also set `OTHER`; they are never discarded. `raw_flags` is always preserved on success. Zero raw flags produce `causes=NONE`, not a fabricated power-on cause. Null definitions/output return `OAMR_STATUS_INVALID`; a non-null output is zeroed on failure.

The adapter owns the actual reset capture/clear order and correct bit definitions. The decoder cannot prove that an adapter captured all flags or chose the right STM32 variant. A named cause, including watchdog reset, is diagnostic evidence only and cannot trigger a watchdog or recovery policy.

## Fixed fault-record schema

The serialized record is exactly **44 bytes**, little-endian, with no native-structure padding or aliasing in its wire/persistence representation:

| Byte offset | Size | Field |
|---:|---:|---|
| 0 | 4 | magic `0x4f414652`, bytes `52 46 41 4f` (`OAFR`) |
| 4 | 2 | version = 1 |
| 6 | 2 | total length = 44 |
| 8 | 4 | raw reset flags |
| 12 | 4 | stacked PC |
| 16 | 4 | stacked LR |
| 20 | 4 | stacked xPSR |
| 24 | 4 | CFSR |
| 28 | 4 | HFSR |
| 32 | 4 | BFAR |
| 36 | 4 | MMFAR |
| 40 | 4 | CRC-32/ISO-HDLC over bytes `[0,40)` |

CRC parameters are reflected polynomial `0xedb88320`, initial value/final XOR `0xffffffff`; the standard ASCII `123456789` check value is `0xcbf43926`. `oamr_fault_record_init` fills the fixed header, copies supplied fault values, retains reset decode definitions as in-memory context, and computes the CRC. `oamr_fault_record_encode` writes exactly 44 bytes and never truncates into a smaller destination. `oamr_fault_record_validate` and `decode` require a complete valid record; trailing bytes are left to the caller's framing. Validation distinguishes bad magic, unsupported version, bad length, CRC mismatch, truncation and malformed arguments. `decode` zeroes a non-null output on failure.

The reset masks are **not serialized**: a next-boot decoder needs the same reviewed board definitions supplied again. The raw flags remain available even if definitions change. Captured zeros are values, not proof that hardware stacked registers existed or that BFAR/MMFAR were valid. The adapter must capture the correct exception stack and SCB registers; this module does not infer exception type, address validity, fault freshness, replay protection or causality.

**No persistence or backup-power claim is made.** These functions can encode, validate and decode bytes in caller-owned memory only. They do not place a `.noinit`/Backup SRAM section, initialize the backup domain, prove retention across reset/power loss, erase a record, or guarantee atomic update against interruption. Any retained record needs a separate reviewed commit/erase protocol and linker/memory proof. A valid CRC proves consistency of the supplied bytes, not survival through every reset, correct hardware capture or backup supply.

## Bounded nonblocking diagnostic TX

`oamr_diagnostic_tx_init` binds caller-owned byte storage to the PR 3 nonblocking `oamr_transport_write_callback_t`. No internal retry, polling loop, allocation or delay exists. All operations require external serialization if shared between execution contexts; the ring is not lock-free.

`oamr_diagnostic_tx_submit` accepts a nonempty complete byte record only when the entire record fits in remaining capacity. Otherwise it returns `OAMR_STATUS_OVERFLOW`, rejects the new record without changing queued bytes, and saturating-increments `dropped_records`/`dropped_bytes`. Null/invalid arguments return `INVALID` and are not counted as capacity drops. A zero-length submission is also rejected as `OVERFLOW`; it does not create a fake record. Copying wraps at most once and old records are never overwritten.

`oamr_diagnostic_tx_flush_once` makes **at most one** callback call and can transfer only the current contiguous prefix. Short successful transfers preserve the remainder. `WOULD_BLOCK`, and a zero-byte `OK` transfer, increment `would_block_calls`, account the currently queued bytes in `would_block_bytes`, and leave the ring unchanged. Transport failures and malformed `OK` transfers increment `transport_errors`/`error_bytes` and retain queued bytes for an explicit later integration policy; they are not mislabeled delivered or capacity-dropped. Byte, record, error and would-block counters saturate. Byte counters may repeat accounting of the same queued bytes across calls; they are event accounting, not a unique-loss estimate.

The six report functions format and submit only caller-supplied data:

- `BOOT`: image, board and boot sequence.
- `VERSION`: firmware, revision and configuration strings.
- `CLOCK`: SYSCLK/HCLK/PCLK1/PCLK2 values in Hz.
- `PERIPH`: caller's name, PR 3 status and detail value.
- `RESET`: raw flags, decoded cause mask/names and unknown raw flags.
- `FAULT`: all fixed-schema fields, CRC and canonical reset decode context.

Each formatted line has a fixed `OAMR ` prefix, `key=value` fields and CRLF, with no `stdio`, heap or unbounded logging. The formatter buffer is fixed at 512 bytes. Required null strings/pointers or excessive formatted length return `INVALID`/`OVERFLOW` and increment `formatting_errors`; no partial report is queued. Capacity rejection is reported separately by submit. Raw transport bytes can be submitted with `submit`; no boot, peripheral, fault or reset message is fabricated merely because TX was initialized. In particular there is no watchdog or actuator-state report type.

## Exact portable acceptance matrix

A host test target must compile all three C files with the implemented PR 3 common/transport headers under strict C11 warnings in Debug and Release, with an additional ASan/UBSan run. Required cases are:

1. **Reset definitions:** zero flags, each of seven named causes and board OTHER separately, all eight simultaneously, arbitrary non-STM32 test masks, deliberately overlapping masks, known-plus-unknown and unknown-only raw bits. Assert exact raw/cause/unknown values and invalid/null behavior.
2. **Fault CRC/layout:** standard CRC vector, exact 44 bytes and all field offsets, high-bit and zero values, one-bit corruption at every byte offset, larger output/input buffers, and no implicit use of native padding or unaligned loads.
3. **Fault rejection:** bad magic, version, declared length, stored CRC and sizes 0/1/7/8/39/40/43; null data with both zero/nonzero size; invalid encode/decode arguments and one-byte-short output. Verify exact validation reason and zeroed failed decode.
4. **Formatter golden lines:** all six report types, simultaneous reset names, unknown causes, zero/maximum numeric values, null required fields, too-long strings and exact CRLF/length. Assert no report is emitted from absent data and no unintended watchdog/actuator field exists.
5. **Ring bounds/wrap:** capacity one, exact fit, one-byte-too-large, full/new-record rejection, empty pop equivalent, FIFO byte order through multiple wraps, short writes and a full drain. Existing bytes and counters must remain coherent after every rejected submission.
6. **Backpressure/errors:** would-block and zero-progress calls never invoke internal retries or discard bytes; one callback at most per flush; short progress; disconnect/timeout/error/malformed callback transfer behavior; exact cumulative counters and saturation fixtures. Transport mocks must record every call to prove boundedness.
7. **Misuse/integrity:** invalid initialization, null arguments, corrupted ring metadata and snapshot failure. Add guard bytes around storage/format buffers and an intentionally broken mock check proving the harness can fail.
8. **Scope:** inspection finds no HAL/include dependency, dynamic allocation, blocking delay/poll loop, RCC constants, watchdog/safety-control operation or competing PR 3 status/transport types.

Acceptance is only portable software acceptance when those tests pass without sanitizer diagnostics. Actual test counts and commands belong in the coordinator's progress record after the dedicated test project is integrated; this document does not relabel temporary development checks as that acceptance suite.

Initial package-author development checks included strict host C11 compilation, standalone Cortex-M7 compilation, cppcheck and a temporary sanitizer smoke harness. They are historical notes only. The authoritative current results are the coordinator-integrated CTest/static/target evidence recorded below and in `IMPLEMENTATION_PROGRESS.md`; no hardware test is claimed by either stage.

## Coordinator integration status — verified diagnostic subset

The separate USART3 adapter and build integration now exist, with strict host tests and fresh Debug/Release builds. The final coordinator acceptance is recorded in `IMPLEMENTATION_PROGRESS.md`: diagnostics CTest 4/4 and 31 cases/3281 checks in each sanitized and non-sanitized host mode; full host CTest 17/17 in each mode; cppcheck 47 files plus a separate adapter check; both embedded memory checks; Debug 90492 bytes and Release 52900 bytes. The UART free-space conditional uses explicit unsigned arithmetic without changing its reserved empty slot. A golden-output test also found and confirmed correction of an extra comma after `OAMR FAULT `.

This is a **verified diagnostic subset, not full PR 6 completion**. Raw RCC capture before clear, exact adapter masks and nonblocking USART3 linkage are implemented in source and host/static/build tested, but physical behavior remains unverified. There is still no exception-stack/SCB capture, retained fault-storage section, interrupted-write policy or next-boot fault report/clear integration. The temporary development claims above are historical package-author notes, not the coordinator acceptance record.

The reviewed integration must define whether fault reporting occurs from a copied valid record in normal application context, how interrupted/partial retained writes are rejected, and who clears/replaces a reported record. A cleared report must not erase the only diagnostic evidence accidentally. No automatic motion recovery or watchdog refresh may be attached to reporting.

The existing sensor-startup terminal path calls `Error_Handler()`, which disables interrupts and loops forever. Queued startup diagnostics can therefore remain undelivered, and an active interrupt-driven transfer need not complete. This path makes diagnostics best-effort, not guaranteed output; the integration explicitly preserves that limitation rather than adding a blocking transmit fallback or changing the established error-handler behavior. A guaranteed-delivery policy requires separate review and testing.

## Bench limitations and outstanding Level 4 work

**No physical STM32H723, USART3 VCP, reset controller, backup domain or fault injection has been exercised by these portable files.** Host tests cannot prove electrical output, actual RCC values, stacked exception registers, retention or safe behavior during a fault.

Supervised bench/integration evidence remains required for:

- PD8/PD9 routing, ST-LINK VCP presence and measured 115200 8N1 output under early-boot and sustained backpressure; demonstrate bounded application behavior and observable drop/error accounting.
- Independently induced power-on, pin, software, IWDG, WWDG, low-power, BOR and practical simultaneous/unknown reset fixtures, with debugger/raw-register captures before and after clear. Watchdog setup/testing itself remains separately authorized work.
- Deliberate supervised HardFault/MemManage/BusFault/UsageFault captures verifying stacked PC/LR/xPSR, CFSR/HFSR/BFAR/MMFAR and validity semantics against debugger/register evidence, including a corrupted/truncated stored record.
- Backup SRAM supply/backup-domain configuration, reset-by-reset and true power-loss retention, interrupted-write handling and the actual linker section. `.noinit` alone is not power-loss evidence.
- Both embedded configurations, map/static analysis, stack/fault-path resource use and release behavior. Fault-time logging must be bounded and must not be assumed reliable when the stack, memory or clocks are damaged.

Until those records exist, physical diagnostics, reset classification, fault capture and retention are **BENCH GATED**, and no backup-power persistence, watchdog recovery or safety behavior is claimed.
