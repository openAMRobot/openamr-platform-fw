# PR 3 implemented portable interfaces

**Status:** coordinator-selected implementation contract; actual verification is recorded in `IMPLEMENTATION_PROGRESS.md`  
**Design coverage:** §1, §2.4, §3, §5.5, §6, §8 and §12 of `STM32H723_FIRMWARE_DESIGN.md`; RT-01 and dependent neutral interfaces.

This is the authoritative PR 3 interface description. `PR3_PORTABLE_INTERFACES.md` is a superseded initial proposal and must not be used as an API reference.

## Scope

The implementation provides C11, HAL-independent types and functions for:

- Explicit operation status (`oamr/status.h`).
- Validation of caller-supplied 64-bit microsecond timestamps (`time.h`, `time.c`).
- Pure freshness classification with caller-selected age limits (`freshness.h`, `freshness.c`).
- Fixed-capacity queues of fixed-size values in caller-owned storage (`bounded_queue.h`, `bounded_queue.c`).
- Classical CAN data-frame metadata with inline payload (`can_frame.h`).
- Generic scalar measurement values and metadata (`sample.h`).
- Nonblocking byte-transport callback declarations (`firmware/transport/include/oamr/transport.h`).

No allocation, device scale, safety state, actuator permission, watchdog, command recovery, pin, protocol identifier, sensor threshold, or ROS semantics are introduced. Fresh or structurally valid data is not automatically healthy or safe.

## Status and time

`OAMR_STATUS_INVALID` is zero, so zero initialization is not success. `OAMR_STATUS_OK` denotes explicit success but does not establish freshness.

`oamr_time_validator_t` stores the last accepted timestamp. The first timestamp establishes the baseline; equal timestamps are valid; decreasing timestamps return `INVALID` without altering the last accepted timestamp. This does not read, extend, clamp or repair a hardware clock. Callers must provide monotonically nondecreasing `uint64_t` microseconds and externally serialize shared validator access.

Freshness requires explicit timestamp validity. A future timestamp is invalid. Age equal to `max_age_us` is fresh; greater age is stale. A zero maximum age accepts only an equal timestamp. Subtraction avoids deadline-overflow errors. A valid timestamp at zero is representable.

## Fixed-size bounded queue

`oamr_bounded_queue_init(queue, storage, storage_size, element_size, capacity)` uses caller-owned storage. Capacity is an **element count**, not bytes. It rejects null/zero arguments, multiplication overflow and insufficient storage. Storage bytes are not cleared. Failed initialization leaves a non-null queue zeroed and uninitialized.

- `push` byte-copies one element. Full returns `OVERFLOW`, preserves existing elements, rejects the new element and saturates the overflow counter at `UINT64_MAX`.
- `pop`/`peek` return the oldest element. Empty returns `EMPTY` and leaves the output untouched.
- `reset` retains storage/capacity, empties the queue and clears overflow accounting; stored bytes are not erased.
- Null, uninitialized or detected-corrupt objects return `INVALID`; count/capacity/overflow accessors return zero.
- Element buffers passed to operations must not overlap queue storage. The implementation copies bytes, so no alignment or pointee-lifetime semantics are imposed beyond the actual object representation supplied by the caller.
- All accesses require external serialization if an ISR/thread can share a queue. The type is not lock-free and performs no interrupt masking or waiting.

## CAN frame

`oamr_can_frame_t` is a Classical CAN data-frame value with inline eight-byte payload, identifier/format, DLC, timestamp and local error flags. `oamr_can_frame_is_valid` checks only structure: non-null, DLC 0–8, standard identifier at most `0x7FF` or extended identifier at most `0x1FFFFFFF`. Timestamp validity is carried separately by consumers. Error metadata does not replace protocol or freshness validation. The native C structure is not a packed wire format and must not be serialized directly.

## Sample

`oamr_sample_t` carries a scalar `float`, descriptive unit, explicit `oamr_status_t`, timestamp and validity flag, and producer-local 32-bit sequence. The value is meaningful only with `OAMR_STATUS_OK`; freshness is checked separately. No unit conversion, calibration, finite-value check, sequence replay/wrap rule, source ID, robot transform or safety classification is imposed by this neutral type.

## Nonblocking transport binding

`oamr_transport_t` binds adapter context and optional read/write callbacks. Null callbacks denote unsupported directions and must not be invoked. No wrapper, lifecycle manager, allocation or retry behavior is provided in PR 3.

Callbacks return `oamr_transport_result_t` and take an explicit transferred-byte output:

- They set the transferred count to zero on entry and return immediately without internal polling/retry.
- A zero-length request permits null data and reports zero bytes on success.
- A nonzero request requires data. `OK` reports 1..requested bytes; short progress is valid.
- `WOULD_BLOCK`, `DISCONNECTED`, `TIMEOUT`, `INVALID` and `ERROR` report zero transferred bytes.
- Timeout reports an already bounded adapter transaction; it does not authorize the callback to wait.
- Buffer/count pointers are borrowed only for the call and must not be retained. Callers own storage and synchronization.

Concrete adapters are responsible for enforcing this contract. Host mocks can check conventions but cannot establish actual target nonblocking behavior.

## Build and test boundary

The three production `.c` modules are compiled directly into host tests and into the STM32 `oamr_common` static library in Debug and Release. Host tests must cover invalid/null/zero inputs, arithmetic extremes, queue wrap/full/reset/corruption, timestamp regressions, freshness deadlines, CAN bounds, sample metadata and mocked transport behavior. Cross-compilation without application consumers does not prove runtime use; hardware verification remains separate.
