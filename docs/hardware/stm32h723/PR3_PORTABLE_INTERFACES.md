# PR 3 Portable Interface Contract

> **Superseded draft:** this file records the initial Agent A proposal, not the
> delivered interface. Its pointer-queue, callback-clock and transport-wrapper
> APIs conflict with the minimal implementation selected during integration.
> The authoritative delivered contract is documented in
> `PR3_IMPLEMENTED_INTERFACES.md`. Do not implement against the API listings in
> this draft; its requirement analysis and exclusions remain useful background.

**Status:** implementation and verification specification; no execution evidence is recorded here  
**Normative inputs:** `PR_IMPLEMENTATION_ROADMAP.md` PR 3; `REQUIREMENTS_TRACEABILITY.md` RT-01–RT-11 and RT-19–RT-28; `STM32H723_FIRMWARE_DESIGN.md` §1, §2.4, §3, §5.5, §6, §8 and §12  
**Implementation scope:** only the files listed in §2

This document fixes the first minimal HAL-independent C11 vocabulary for time, freshness, bounded pointer queues, Classical CAN frames, scalar samples, statuses and nonblocking byte transports. It supports the cited requirements; it does not close device, board, protocol or hardware rows by itself. Host success is L2 evidence and target compile/link success is L1 evidence, not L4 verification.

## 1. Scope and exclusions

The contract provides deterministic storage and explicit invalid/stale/error outcomes. It does **not** define or imply:

- E-01 or any other safety state, command-timeout recovery, watchdog behavior or automatic recovery of permission;
- E-stop, brake, contactor, motor-enable, charging/KCHG, lift-permit or other actuator semantics;
- GPIO names or pin numbers, sensor safety thresholds, range gates, command thresholds or stale-timeout defaults;
- CAN bit timing, filters, bus-off recovery, CANopen/Daly/ZLAC/CTL-017 payload semantics, or lift messages;
- sensor scaling, IMU registers/axes, micro-ROS entities, ROS types, network addresses or transport connection policy;
- a diagnostic sink, heap allocator, general ring of bytes, RTOS primitives or an interrupt-safe locking scheme.

`max_age_us`, transport retry/reconnect behavior, source IDs and all device limits are supplied by the caller or a later reviewed module. A fresh or transport-valid value is not thereby healthy, safe or authorized for actuation.

## 2. Exact file set

The first implementation shall add only these production interface files, plus its separately owned tests/build registration:

| File | Contents |
|---|---|
| `firmware/common/include/oamr/status.h` | Shared status enum; header-only |
| `firmware/common/include/oamr/time.h` | 64-bit microsecond time type and checked caller-supplied clock |
| `firmware/common/include/oamr/freshness.h` | Pure timestamp-age classification |
| `firmware/common/include/oamr/bounded_queue.h` | Fixed-capacity queue of `void *` values |
| `firmware/common/include/oamr/can_frame.h` | Classical CAN data-frame value type and metadata; header-only |
| `firmware/common/include/oamr/sample.h` | Scalar sample value type and metadata; header-only |
| `firmware/common/src/time.c` | Time-source initialization and checked reads |
| `firmware/common/src/freshness.c` | Freshness classifier |
| `firmware/common/src/bounded_queue.c` | Queue operations |
| `firmware/transport/include/oamr/transport.h` | Nonblocking byte-transport interface and small inline wrappers |

There shall be no duplicate `firmware/common/include/oamr/transport.h`. The only transport header is `firmware/transport/include/oamr/transport.h`. No `sample.c`, `can_frame.c`, `status.c`, `diagnostic.*`, board adapter or production transport `.c` is part of this minimal package.

## 3. Common rules

### 3.1 Language, representation and allocation

- Public code is C11, includes only standard C headers and other listed `oamr/` headers, and has no STM32 HAL, ROS, RTOS or host-libc dependency.
- Fixed-width integers are used for timestamps, identifiers and sequence numbers. Structs and enums are native C values, **not packed wire formats**; padding and enum representation shall not be serialized directly.
- No operation allocates, reallocates, frees or resizes memory. No operation takes ownership of a general-heap allocation.
- Struct fields named below are implementation state except where a type is explicitly described as a value type. Callers shall not modify or copy an active time source, queue or transport. `init_cookie` is private to the implementation.
- Integer enum values are fixed for source-level clarity, but do not constitute a communication protocol version or ABI promise.

### 3.2 Initialization and failure atomicity

`oamr_time_source_t`, `oamr_bounded_queue_t` and `oamr_transport_t` shall be initialized with their corresponding `{ 0 }` initializer before their first `init` call. Static zero initialization is also valid. Indeterminate automatic storage is not accepted.

For each stateful type:

1. A null object pointer returns `OAMR_STATUS_INVALID_ARGUMENT`.
2. Calling `init` on an already active object returns `OAMR_STATUS_INVALID_STATE` and leaves it unchanged. There is no implicit deinitialization or forced reinitialization.
3. Invalid first-initialization arguments leave the object in its zero/uninitialized state.
4. Any operation on a zero/uninitialized object returns `OAMR_STATUS_INVALID_STATE`. The cookie detects the documented zero/sentinel misuse; it does not make arbitrary corrupted memory safe.
5. Required output pointers are validated before use. Except for transport callbacks, a failed operation leaves caller outputs and object state unchanged. Output parameters are never used as hidden status returns.
6. Objects and all supplied storage/callback contexts shall outlive every user of the interface. Initialization may occur during application setup, not concurrently with use.

There is no dynamic teardown API. A caller may abandon an inactive object only when no execution context can access it. Queue reset, defined below, does not deinitialize the queue.

### 3.3 Status and concurrency

`OAMR_STATUS_OK` is the only successful status for a stateful operation. Queue `EMPTY`/`FULL` are expected bounded outcomes and never overwrite an item. `STALE` and sample validity deliberately use their own types rather than overloading an operation status.

The portable queue and time source are **not internally synchronized or lock-free**. All accesses to one instance, including ISR/task access, shall be externally serialized with a reviewed nonblocking target policy. The portable module does not disable interrupts, take a mutex or claim C11 concurrent execution safety. A transport wrapper likewise supplies no synchronization; any stronger callback guarantee must be established by its concrete adapter. Host tests shall not claim IRQ concurrency proof.

## 4. `oamr/status.h`

```c
#ifndef OAMR_STATUS_H
#define OAMR_STATUS_H

typedef enum oamr_status {
    OAMR_STATUS_OK = 0,
    OAMR_STATUS_INVALID_ARGUMENT = 1,
    OAMR_STATUS_INVALID_STATE = 2,
    OAMR_STATUS_EMPTY = 3,
    OAMR_STATUS_FULL = 4,
    OAMR_STATUS_WOULD_BLOCK = 5,
    OAMR_STATUS_DISCONNECTED = 6,
    OAMR_STATUS_TIMEOUT = 7,
    OAMR_STATUS_OVERFLOW = 8,
    OAMR_STATUS_IO_ERROR = 9
} oamr_status_t;

#endif
```

No status-to-string function or localization layer is introduced. `INVALID_ARGUMENT` covers null pointers, zero capacity where prohibited and out-of-range sizes. `INVALID_STATE` covers use before initialization, repeated initialization, unsupported transport direction and detected clock regression.

## 5. `oamr/time.h` and `time.c`

### 5.1 API

```c
#ifndef OAMR_TIME_H
#define OAMR_TIME_H

#include <stdint.h>
#include "oamr/status.h"

typedef uint64_t oamr_time_us_t;

typedef oamr_time_us_t (*oamr_time_read_us_fn)(void *context);

typedef struct oamr_time_source {
    void *context;
    oamr_time_read_us_fn read_us;
    oamr_time_us_t last_us;
    uint32_t init_cookie;
} oamr_time_source_t;

#define OAMR_TIME_SOURCE_INITIALIZER { 0 }

oamr_status_t oamr_time_source_init(oamr_time_source_t *source,
                                    void *context,
                                    oamr_time_read_us_fn read_us);
oamr_status_t oamr_time_now_us(oamr_time_source_t *source,
                               oamr_time_us_t *out_now_us);

#endif
```

### 5.2 Time contract

- `read_us` is required; `context` may be null and is passed through unchanged. The callback runs synchronously and shall not retain `source` or any output pointer.
- The caller-supplied clock returns **microseconds on a monotonically nondecreasing `uint64_t` timeline**. It shall extend any narrower hardware counter before returning and shall not truncate at 32 bits. This module does not infer or repair a raw 32-bit wrap.
- The first successful read accepts every `uint64_t` value, including zero, and records it in `last_us`.
- Each later raw value greater than or equal to `last_us` succeeds, is returned exactly and updates `last_us`. Equal values are valid for a clock whose resolution is coarser than the call rate.
- A raw value less than `last_us` returns `OAMR_STATUS_INVALID_STATE`; `out_now_us` and `last_us` remain unchanged. The implementation shall not clamp a regression or fabricate a later timestamp. A later callback value at or above the unchanged `last_us` may succeed.
- `source == NULL`, `read_us == NULL` during initialization, or `out_now_us == NULL` returns `INVALID_ARGUMENT` as applicable. A failed `now` call writes no timestamp.
- All storage, callback and comparison arithmetic is 64-bit. In particular, `0x00000000FFFFFFFF` followed by `0x0000000100000000` is forward progress, not wrap or regression.
- At `UINT64_MAX`, an equal read remains valid and every lower subsequent value is a regression. No 64-bit addition is used to extrapolate time. This contract does not claim an infinite clock.

## 6. `oamr/freshness.h` and `freshness.c`

### 6.1 API

```c
#ifndef OAMR_FRESHNESS_H
#define OAMR_FRESHNESS_H

#include <stdbool.h>
#include "oamr/time.h"

typedef enum oamr_freshness {
    OAMR_FRESHNESS_INVALID = 0,
    OAMR_FRESHNESS_FRESH = 1,
    OAMR_FRESHNESS_STALE = 2
} oamr_freshness_t;

oamr_freshness_t oamr_freshness_evaluate(bool has_timestamp,
                                         oamr_time_us_t sample_timestamp_us,
                                         oamr_time_us_t now_us,
                                         oamr_time_us_t max_age_us);

#endif
```

### 6.2 Exact classification

The function is pure and evaluates the following ordered rules:

1. If `has_timestamp` is false, return `INVALID`; all timestamp and age bits are ignored.
2. If `now_us < sample_timestamp_us`, return `INVALID`. A future-dated sample is not fresh or merely stale.
3. Otherwise compute age as `now_us - sample_timestamp_us`, which cannot underflow after rule 2.
4. Return `FRESH` when `age <= max_age_us`; return `STALE` when `age > max_age_us`.

Thus the deadline is **inclusive**: at exactly `sample_timestamp_us + max_age_us` the value is fresh, and one microsecond later it is stale. Implement this with subtraction/comparison, not `sample_timestamp_us + max_age_us`, so a deadline near `UINT64_MAX` cannot overflow.

`max_age_us == 0` is valid: `now_us == sample_timestamp_us` is fresh and every later time is stale. A timestamp of zero with `has_timestamp == true` is valid; absence is represented only by the boolean, never by a magic timestamp. `UINT64_MAX` is a valid maximum age. Freshness examines timestamps only; an invalid sample value remains invalid even if this function returns `FRESH`.

## 7. `oamr/bounded_queue.h` and `bounded_queue.c`

### 7.1 API

```c
#ifndef OAMR_BOUNDED_QUEUE_H
#define OAMR_BOUNDED_QUEUE_H

#include <stddef.h>
#include <stdint.h>
#include "oamr/status.h"

typedef struct oamr_bounded_queue {
    void **storage;
    size_t capacity;
    size_t head;
    size_t count;
    uint32_t init_cookie;
} oamr_bounded_queue_t;

#define OAMR_BOUNDED_QUEUE_INITIALIZER { 0 }

oamr_status_t oamr_bounded_queue_init(oamr_bounded_queue_t *queue,
                                      void **storage,
                                      size_t capacity);
oamr_status_t oamr_bounded_queue_push(oamr_bounded_queue_t *queue,
                                      void *item);
oamr_status_t oamr_bounded_queue_pop(oamr_bounded_queue_t *queue,
                                     void **out_item);
oamr_status_t oamr_bounded_queue_count(
    const oamr_bounded_queue_t *queue,
    size_t *out_count);
oamr_status_t oamr_bounded_queue_reset(oamr_bounded_queue_t *queue);

#endif
```

### 7.2 Capacity is a pointer-element count, not bytes

`storage` is a caller-owned array of exactly `capacity` pointer slots. Its required byte extent is at least:

```c
capacity * sizeof(*storage) /* equivalently capacity * sizeof(void *) */
```

`capacity` is **not** a payload-byte size and does not count the bytes in any pointed-to object. Initialization rejects null `storage`, zero capacity and `capacity > SIZE_MAX / sizeof(void *)` with `INVALID_ARGUMENT`. The final check ensures that the byte extent and internal index arithmetic are representable. The caller remains responsible for supplying a real array of the declared count, suitably aligned for `void *`.

A typical declaration is:

```c
static void *frame_slots[16];
static oamr_bounded_queue_t frames = OAMR_BOUNDED_QUEUE_INITIALIZER;
/* init(&frames, frame_slots, 16): sixteen frame pointers, not sixteen bytes. */
```

### 7.3 Operations and ownership

- `init` records the external array and starts empty with `head == count == 0`. It does not clear the array or any pointed-to object.
- `push` rejects `item == NULL` with `INVALID_ARGUMENT`. Therefore every successfully popped item is non-null. If `count == capacity`, it returns `FULL` without eviction, overwrite or state change. Otherwise it copies only the pointer value at the FIFO tail and increments `count`.
- `pop` requires non-null `out_item`. If empty, it returns `EMPTY`, leaves `*out_item` unchanged and does not alter indexes. Otherwise it returns the oldest pointer, advances the head with wraparound and decrements `count`.
- `count` requires non-null `out_count`; on success it returns the exact number of occupied pointer slots, including zero for an empty queue. It cannot confuse an invalid queue with an empty queue because invalid state is a status error and the output remains unchanged.
- `reset` sets `head` and `count` to zero and leaves `storage` and `capacity` unchanged. Old slots may still contain pointer bits but are unreachable through the queue. Reset does not invoke destructors, free pointees or make a concurrent reset safe.
- Push/pop wrap without an ambiguous full/empty flag: `count` alone distinguishes those states. Failed operations do not modify storage, head or count.

The queue owns **neither the pointer array nor pointees**. After a successful push, the producer shall keep the pointed-to object alive and its contents stable until a successful pop transfers that borrowed reference to the consumer, or until reset/abandonment returns responsibility to the producer. After pop, the slot no longer references the object and the consumer must honor the producer's lifetime/recycling agreement. The API performs no payload copy and provides no destructor.

This is a typed-by-the-caller generic pointer queue, not a byte stream. A consumer shall use the same pointed-to type established by its producer. No concurrent push/pop is permitted without the external serialization required in §3.3.

## 8. `oamr/can_frame.h`

### 8.1 API

```c
#ifndef OAMR_CAN_FRAME_H
#define OAMR_CAN_FRAME_H

#include <stdbool.h>
#include <stdint.h>
#include "oamr/time.h"

#define OAMR_CAN_MAX_DATA_LENGTH 8u
#define OAMR_CAN_STANDARD_ID_MAX UINT32_C(0x000007FF)
#define OAMR_CAN_EXTENDED_ID_MAX UINT32_C(0x1FFFFFFF)

typedef enum oamr_can_id_format {
    OAMR_CAN_ID_STANDARD = 0,
    OAMR_CAN_ID_EXTENDED = 1
} oamr_can_id_format_t;

typedef uint32_t oamr_can_error_flags_t;

#define OAMR_CAN_ERROR_NONE        UINT32_C(0x00000000)
#define OAMR_CAN_ERROR_WARNING     UINT32_C(0x00000001)
#define OAMR_CAN_ERROR_PASSIVE     UINT32_C(0x00000002)
#define OAMR_CAN_ERROR_BUS_OFF     UINT32_C(0x00000004)
#define OAMR_CAN_ERROR_TX_FAILURE  UINT32_C(0x00000008)
#define OAMR_CAN_ERROR_RX_OVERFLOW UINT32_C(0x00000010)

typedef struct oamr_can_frame {
    uint32_t id;
    oamr_can_id_format_t id_format;
    uint8_t dlc;
    uint8_t data[OAMR_CAN_MAX_DATA_LENGTH];
    bool has_timestamp;
    oamr_time_us_t timestamp_us;
    oamr_can_error_flags_t error_flags;
} oamr_can_frame_t;

#endif
```

### 8.2 Frame contract

- This value type represents **Classical CAN data frames only**, with zero through eight data bytes. There is no CAN FD payload, bit-rate-switch, FD-format or remote-frame encoding. An adapter shall not silently truncate a DLC greater than eight or an unsupported frame into this type.
- `id` contains only the identifier, not format flag bits. Standard IDs are `0..0x7FF`; extended IDs are `0..0x1FFFFFFF`. Any other `id_format` value or an ID above the selected format's maximum is invalid and must be rejected at a construction/adapter boundary.
- `dlc` is the number of valid bytes in `data`, `0..8`. DLC zero is valid and has no payload bytes. Producers shall zero unused payload bytes, especially before transmit, so stale data is not leaked. Consumers shall inspect only `data[0..dlc-1]`.
- `has_timestamp == true` means `timestamp_us` is the local receive/transmit observation time on the common 64-bit microsecond timeline; zero is then a valid timestamp. If false, no timestamp exists and `timestamp_us` shall be ignored and canonically zero.
- `error_flags` is local metadata associated with the observed frame or attempted TX frame; it is not transmitted on CAN and is not a wire CAN error-frame representation. Bits may be ORed. Unknown bits shall be preserved by value-copy operations and rejected by a boundary that cannot represent them.
- `OAMR_CAN_ERROR_NONE` means only that no listed local error is attached. It does not establish payload correctness, freshness, sensor validity or permission to act. Any nonzero error metadata makes the record diagnostic/invalid for normal protocol decoding until the relevant adapter has explicitly handled it.
- The value type is copied by value and owns no buffers. A frame placed in the pointer queue still requires caller-managed frame-object lifetime as described in §7.3.
- `{ 0 }` produces a standard ID 0, DLC 0 data frame with absent timestamp and no attached errors. ID 0 has no protocol meaning assigned by this contract.

CAN acceptance filters, malformed-frame counters and bus-off state machines belong to PR 5 or later. This header does not choose any device ID or decode any payload.

## 9. `oamr/sample.h`

### 9.1 API

```c
#ifndef OAMR_SAMPLE_H
#define OAMR_SAMPLE_H

#include <stdbool.h>
#include <stdint.h>
#include "oamr/time.h"

typedef enum oamr_sample_unit {
    OAMR_SAMPLE_UNIT_UNSPECIFIED = 0,
    OAMR_SAMPLE_UNIT_METRE = 1,
    OAMR_SAMPLE_UNIT_MILLIMETRE = 2,
    OAMR_SAMPLE_UNIT_VOLT = 3,
    OAMR_SAMPLE_UNIT_AMPERE = 4,
    OAMR_SAMPLE_UNIT_DEGREE_CELSIUS = 5,
    OAMR_SAMPLE_UNIT_PERCENT = 6,
    OAMR_SAMPLE_UNIT_METRE_PER_SECOND_SQUARED = 7,
    OAMR_SAMPLE_UNIT_RADIAN_PER_SECOND = 8,
    OAMR_SAMPLE_UNIT_RAW = 9
} oamr_sample_unit_t;

typedef struct oamr_sample {
    float value;
    oamr_sample_unit_t unit;
    bool valid;
    bool has_timestamp;
    oamr_time_us_t timestamp_us;
    uint64_t sequence;
    uint16_t source_id;
} oamr_sample_t;

#endif
```

### 9.2 Sample contract

- `value` is a scalar `float` in the declared unit. `METRE`, `MILLIMETRE`, `VOLT`, `AMPERE`, `DEGREE_CELSIUS`, acceleration and angular-rate units have their ordinary meanings; `PERCENT` uses 0–100 notation. `RAW` is a device-native finite scalar whose scaling must be documented by its producer. The enum does not select any sensor's output unit or scaling.
- `valid` concerns the numeric value and its unit only. A producer may set it true only after validation, with a finite `value` and a recognized unit other than `UNSPECIFIED`. A numeric zero can be valid; zero is not an invalid-data sentinel. NaN and infinities shall not be marked valid.
- If `valid` is false, the value shall not be consumed as a measurement. Canonical invalid records use `value == 0.0f` and `UNSPECIFIED`; transport/protocol errors shall not be converted into a valid zero measurement.
- Timestamp presence is independent of numeric validity. `has_timestamp` and `timestamp_us` have the same semantics as for CAN. A sample with a valid value but no timestamp still evaluates as freshness `INVALID`. Normal measurement consumption requires `valid`, a present timestamp and fresh/non-regressed time.
- `sequence` identifies successive records published by one producer/source. It starts at zero and advances by one for each newly published record (valid or invalid). Full-64-bit wrap is defined modulo `2^64`, so `UINT64_MAX` advances to zero; no 32-bit sequence truncation is allowed. Duplicate redelivery, replay detection and out-of-order acceptance require a separate producer protocol contract and are not inferred from this metadata.
- `source_id` distinguishes independent producers within a consuming composition. Zero is a valid source ID, not an absent-source marker. The composition owner assigns IDs; this document defines no global device/pin/source table. Sequence numbers from different sources are never compared as one stream.
- The sample is copied by value and owns no memory. `{ 0 }` is an invalid, unit-unspecified sample with no timestamp, sequence zero and source ID zero.

No freshness timeout, physical range, robot-axis transform or safe/healthy conclusion is built into this type.

## 10. `oamr/transport.h`

Path: `firmware/transport/include/oamr/transport.h`.

### 10.1 API

```c
#ifndef OAMR_TRANSPORT_H
#define OAMR_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>
#include "oamr/status.h"

typedef struct oamr_transport_result {
    oamr_status_t status;
    size_t count;
} oamr_transport_result_t;

typedef oamr_transport_result_t (*oamr_transport_read_fn)(
    void *context,
    uint8_t *data,
    size_t capacity);

typedef oamr_transport_result_t (*oamr_transport_write_fn)(
    void *context,
    const uint8_t *data,
    size_t size);

typedef struct oamr_transport {
    void *context;
    oamr_transport_read_fn read;
    oamr_transport_write_fn write;
    uint32_t init_cookie;
} oamr_transport_t;

#define OAMR_TRANSPORT_INITIALIZER { 0 }

static inline oamr_transport_result_t oamr_transport_detail_result(
    oamr_status_t status,
    size_t count)
{
    oamr_transport_result_t result;
    result.status = status;
    result.count = count;
    return result;
}

static inline oamr_transport_result_t oamr_transport_detail_check(
    oamr_transport_result_t result,
    size_t requested)
{
    switch (result.status) {
    case OAMR_STATUS_OK:
        if ((result.count > 0u) && (result.count <= requested)) {
            return result;
        }
        break;
    case OAMR_STATUS_WOULD_BLOCK:
    case OAMR_STATUS_DISCONNECTED:
    case OAMR_STATUS_TIMEOUT:
    case OAMR_STATUS_OVERFLOW:
    case OAMR_STATUS_IO_ERROR:
        if (result.count == 0u) {
            return result;
        }
        break;
    case OAMR_STATUS_EMPTY:
    case OAMR_STATUS_FULL:
        if (result.count == 0u) {
            return oamr_transport_detail_result(OAMR_STATUS_WOULD_BLOCK, 0u);
        }
        break;
    default:
        break;
    }
    return oamr_transport_detail_result(OAMR_STATUS_IO_ERROR, 0u);
}

static inline oamr_status_t oamr_transport_init(
    oamr_transport_t *transport,
    void *context,
    oamr_transport_read_fn read,
    oamr_transport_write_fn write)
{
    if (transport == NULL) {
        return OAMR_STATUS_INVALID_ARGUMENT;
    }
    if (transport->init_cookie != 0u) {
        return OAMR_STATUS_INVALID_STATE;
    }
    if ((read == NULL) && (write == NULL)) {
        return OAMR_STATUS_INVALID_ARGUMENT;
    }
    transport->context = context;
    transport->read = read;
    transport->write = write;
    transport->init_cookie = UINT32_C(0x54525031);
    return OAMR_STATUS_OK;
}

static inline oamr_transport_result_t oamr_transport_read(
    const oamr_transport_t *transport,
    uint8_t *data,
    size_t capacity)
{
    if ((transport == NULL) || ((data == NULL) && (capacity != 0u))) {
        return oamr_transport_detail_result(OAMR_STATUS_INVALID_ARGUMENT, 0u);
    }
    if ((transport->init_cookie != UINT32_C(0x54525031)) ||
        (transport->read == NULL)) {
        return oamr_transport_detail_result(OAMR_STATUS_INVALID_STATE, 0u);
    }
    if (capacity == 0u) {
        return oamr_transport_detail_result(OAMR_STATUS_OK, 0u);
    }
    return oamr_transport_detail_check(
        transport->read(transport->context, data, capacity), capacity);
}

static inline oamr_transport_result_t oamr_transport_write(
    const oamr_transport_t *transport,
    const uint8_t *data,
    size_t size)
{
    if ((transport == NULL) || ((data == NULL) && (size != 0u))) {
        return oamr_transport_detail_result(OAMR_STATUS_INVALID_ARGUMENT, 0u);
    }
    if ((transport->init_cookie != UINT32_C(0x54525031)) ||
        (transport->write == NULL)) {
        return oamr_transport_detail_result(OAMR_STATUS_INVALID_STATE, 0u);
    }
    if (size == 0u) {
        return oamr_transport_detail_result(OAMR_STATUS_OK, 0u);
    }
    return oamr_transport_detail_check(
        transport->write(transport->context, data, size), size);
}

#endif
```

`count` is always a **byte count**, not an element count. At least one of `read` or `write` is required at initialization; both null is `INVALID_ARGUMENT`. This permits read-only and write-only bounded adapters without dummy callbacks. `context` may be null. Calling an absent direction on an otherwise initialized transport returns `INVALID_STATE, 0`.

### 10.2 Wrapper validation and zero-size behavior

For each direction, in order:

1. Null `transport`, or null `data` with a nonzero request, returns `INVALID_ARGUMENT, 0` and invokes no callback.
2. A zero/uninitialized transport, or an absent requested callback, returns `INVALID_STATE, 0` and invokes no callback.
3. A zero-byte request on an initialized supported direction returns `OK, 0`; `data` may be null and the callback is not invoked. No connection/readiness probe occurs.
4. Otherwise invoke the callback synchronously with the original context, buffer and requested byte count.

A valid callback result obeys this table:

| Callback outcome | Required `count` | Meaning |
|---|---:|---|
| `OK`, nonzero request | `1..requested size` | Bytes read into `data`, or bytes consumed from `data`; partial progress is valid |
| `WOULD_BLOCK` | 0 | No progress can be made immediately; caller may retry later |
| `DISCONNECTED` | 0 | No current connection; no bytes produced/consumed |
| `TIMEOUT` | 0 | An already-managed operation/transaction has timed out; the callback did not sleep until a deadline |
| `OVERFLOW` | 0 | A bounded adapter RX path overflowed/dropped data; no valid partial record is reported |
| `IO_ERROR` | 0 | Other transport/controller failure |

For nonzero requests, `OK, 0`, any count above the requested size, a nonzero count with a non-OK status, or any status outside the table is a callback contract violation. The wrapper normalizes such a result to `IO_ERROR, 0`; it cannot undo side effects of an already defective callback. The wrapper also normalizes every other non-OK status, including an internally forwarded `EMPTY` or `FULL`, to `WOULD_BLOCK, 0` so backpressure has one portable outcome. Queue-backed adapters should map empty RX/full TX directly to `WOULD_BLOCK`.

For `OK` reads, bytes beyond `count` are unchanged. For non-OK results, the callback shall leave the read buffer unchanged and produce/consume no bytes. A partial `OK` write leaves the unsent suffix with the caller; the wrapper never retries automatically.

### 10.3 Nonblocking and buffer ownership

- Callbacks shall not sleep, poll until data/space/connection arrives, wait for a mutex, flush a whole stream or perform an unbounded retry. If immediate bounded progress is impossible they return `WOULD_BLOCK` or `DISCONNECTED`.
- `TIMEOUT` reports existing adapter state, not permission to block in this call. Connection establishment, reopen, reconnect/backoff and close policy are outside this minimal interface.
- Read and write buffers remain caller-owned. A callback may access a buffer only during the synchronous call. On write return it shall have copied/consumed every reported byte and shall retain no pointer to any part of the buffer. Asynchronous TX therefore requires adapter-owned bounded storage or `WOULD_BLOCK`.
- On read return, only the reported prefix is valid for the caller. The callback shall not retain the destination or later write into it.
- The wrapper performs no allocation, copying, framing or blocking. No method is implicitly ISR-safe merely because it is nonblocking; adapter execution context and serialization still require review.
- Disconnection does not synthesize a zero-length successful read/write and does not imply any application recovery or actuator behavior.

## 11. Required host test matrix

Tests belong in an independently configurable `tests/host/common_interfaces/` project using the PR 1 harness, C11 strict warnings and `OAMR_SANITIZE` both ON and OFF where supported. The matrix is normative; every applicable case must execute at least one assertion. Scripted callbacks/fakes are L2 only.

| ID | Cases and required assertions |
|---|---|
| I-01 | For each stateful type, zero-initializer operations return `INVALID_STATE`; required null pointers return `INVALID_ARGUMENT`; outputs/sentinels and storage remain unchanged. |
| I-02 | Valid first initialization succeeds; repeated active initialization returns `INVALID_STATE` without replacing callbacks, context, storage, capacity or queued contents. Failed first initialization leaves a zero object. |
| T-01 | Script raw times `0`, `1`, `UINT32_MAX - 1`, `UINT32_MAX`, `UINT32_MAX + 1` and `UINT64_MAX`; exact 64-bit values are returned with no truncation and equal reads succeed. |
| T-02 | A decreasing raw value returns `INVALID_STATE`, leaves both output and `last_us` unchanged, then an equal/higher raw value recovers without clamping or hidden offset. |
| T-03 | Null time callback/output and uninitialized source follow §5; callback context is passed exactly and a timestamp of zero is representable. |
| F-01 | `has_timestamp == false` returns `INVALID` for zero, ordinary and extreme timestamp/age inputs. |
| F-02 | `now < timestamp` returns `INVALID`, including one-microsecond regression and values around `UINT64_MAX`. |
| F-03 | For timestamp 100 and max age 10: now 100 and 109 are fresh, exact deadline 110 is fresh, and 111 is stale. Also test a nonzero-age boundary at `UINT64_MAX`. |
| F-04 | Max age zero is fresh only at `now == timestamp` and stale at `now == timestamp + 1`; `UINT64_MAX` max age and near-max timestamps do not overflow deadline arithmetic. |
| Q-01 | Reject null queue/storage/count outputs, null item, zero capacity and `SIZE_MAX / sizeof(void *) + 1` capacity as specified. Failed calls preserve sentinel outputs and array canaries. |
| Q-02 | New queue count is zero; empty pop returns `EMPTY` and leaves the output sentinel and indexes unchanged. |
| Q-03 | For capacities 1, 2 and a larger representative count, push exactly capacity distinct object pointers, verify count after each, then verify the next push returns `FULL` and does not evict or overwrite. |
| Q-04 | Pop returns all pointers in exact FIFO order, including null rejection never creating a null entry; final count is zero and another pop is empty. |
| Q-05 | Alternate push/pop across several complete wrap cycles and bursts. Full-after-wrap, empty-after-wrap and full→pop→push cases remain unambiguous and never access outside the supplied pointer array. |
| Q-06 | A two-element array accepts exactly two pointers, proving capacity is not bytes or `2 * sizeof(void *)` elements. Adjacent before/after canary arrays remain unchanged under ASan/UBSan. |
| Q-07 | Reset at empty, partially full and full yields count zero and accepts a new FIFO sequence. It does not clear pointees, invoke a destructor or change external storage/capacity. |
| Q-08 | Static review/instrumented fakes confirm no allocation and no producer payload copy. Tests model borrowed object lifetime and recycling without claiming actual IRQ concurrency. |
| C-01 | Compile-time checks require 8 payload bytes, standard max `0x7FF`, extended max `0x1FFFFFFF` and 64-bit timestamps. Runtime value tests cover DLC 0 and 8, standard/extended extrema and timestamp presence at time zero. |
| C-02 | Contract fixtures for invalid ID format, standard ID `0x800`, extended ID `0x20000000` and DLC 9 are rejected by the test boundary/adapter validator; no truncation occurs. Error bits OR without becoming wire payload and unused TX bytes remain zero. |
| S-01 | Compile-time checks require 64-bit sequence. `{ 0 }` has the canonical invalid/no-timestamp meaning; a finite numeric zero with a specified unit can be valid. NaN, infinities and `UNSPECIFIED` cannot be marked valid. |
| S-02 | Verify every published unit value, sequence 0→1 and duplicate redelivery metadata, independent equal sequences for two sources, valid-value/no-timestamp freshness `INVALID`, and invalid-value/fresh-timestamp remains unusable. |
| R-01 | Transport init rejects null object and both-null callbacks; null context and read-only/write-only variants succeed. Active reinit leaves function pointers/context unchanged; unsupported direction returns `INVALID_STATE, 0`. |
| R-02 | Null data with nonzero size and use before init invoke no callback. Zero-size read/write on each supported direction returns `OK, 0` with null and non-null data and invokes no callback. |
| R-03 | Scripted full and partial reads/writes return exact byte counts; read suffix and unsent write suffix are preserved. A zero count is never accepted as `OK` for a nonzero request. |
| R-04 | Each allowed non-OK transport status returns zero count and unchanged read sentinel; `WOULD_BLOCK`, disconnect, timeout, overflow and I/O error remain distinct. Queue `EMPTY`/`FULL` fakes translate to `WOULD_BLOCK`. |
| R-05 | Callback violations (oversized count, `OK,0`, error plus nonzero count, unexpected status) normalize to `IO_ERROR,0`; invalid inputs never reach the callback. |
| R-06 | A retaining-write fake is rejected by contract review. A conforming asynchronous fake copies the reported prefix, and caller mutation immediately after return cannot alter the copied bytes. Disconnect/reconnect scripts involve no hidden retry or fabricated zero-byte success. |

No test shall add a stale threshold, pin, actuator command, safety state or physical-hardware pass claim to make a case pass.

## 12. Compile, link and static-acceptance criteria

### 12.1 Host

From the repository root, the completed test project shall support at least:

```bash
cmake -S tests/host/common_interfaces -B build/host/common_interfaces -G Ninja
cmake --build build/host/common_interfaces
ctest --test-dir build/host/common_interfaces --output-on-failure --no-tests=error
python3 tools/ci/run_host_tests.py
python3 tools/ci/run_host_tests.py --static-only
```

Run a separate `OAMR_SANITIZE=OFF` configure/build/CTest as required by `tests/host/README.md`. All three production `.c` files shall be compiled directly into the test target and all public functions shall be referenced so the final host link proves there are no missing definitions. GNU/Clang compilation uses the existing strict set:

```text
-Wall -Wextra -Werror -Wconversion -Wshadow -Wstrict-prototypes -Wpedantic
```

Each public header shall also compile as the first/only project include in its own minimal C11 translation unit, proving self-containment and include-path correctness. Include both `firmware/common/include` and `firmware/transport/include` in relevant consumers; no relative `../firmware` include is allowed in production code.

### 12.2 Portable object and embedded link

For the actual PR 3 implementation/integration:

1. Compile all three `.c` files with the STM32H723 Arm GNU C11 flags in both board Debug and Release configurations. Header-only types shall also be included by an embedded smoke consumer so all selected headers are target-checked.
2. Link a freestanding smoke consumer that references every non-inline public function. A relocatable link may check the three objects before board integration, but final acceptance still requires fresh board Debug and Release builds when they are linked, per roadmap PR 3.
3. Both embedded links shall complete with zero new unexplained warnings and no undefined `oamr_*` symbol. Inspect the map enough to show the referenced functions were actually linked, not removed as unreferenced. Record text/data/bss delta and stack artifacts rather than claiming size neutrality.
4. The portable objects shall introduce no HAL/ROS/RTOS/stdio/libm references and no `malloc`, `calloc`, `realloc`, `free` or general heap dependency. Ordinary compiler/runtime support, if any appears, must be identified rather than hidden.
5. Run the existing static analyzer over every new production C/header file. ASan/UBSan success supplements, but never replaces, the non-sanitized host run or either embedded configuration.
6. Re-run applicable existing regressions and record exact commands/results in the coordinator-owned progress log at integration. This specification itself reports no command as executed and no build as passing.

Target linking proves interface/build viability only. It does not prove monotonic hardware-clock extension, IRQ serialization, CAN/UART/USB/Ethernet operation, freshness under real scheduling, electrical behavior or any robot safety property.

## 13. Review checklist

- [ ] Only the selected modules are present; there is no duplicate common transport header or extra diagnostic/safety abstraction.
- [ ] All timestamps and sequences retain 64 bits through the `0xFFFFFFFF` boundary.
- [ ] Missing/future timestamps are freshness-invalid and exact-deadline behavior is inclusive.
- [ ] Queue capacity is pointer-element count, external storage is bounded, null items are rejected and full never overwrites.
- [ ] CAN is Classical data-frame-only with 11/29-bit ID bounds and at most eight bytes.
- [ ] Numeric validity, timestamp presence, freshness and transport status remain distinct; zero is not a universal validity sentinel.
- [ ] Transport callbacks cannot retain caller buffers or block; zero-size calls have deterministic behavior and invalid callback results are contained.
- [ ] Ownership/lifetimes and external serialization are explicit; no host test is presented as IRQ or hardware proof.
- [ ] No safety states, pins, thresholds, actuator permissions, device protocol semantics or unapproved configuration values were introduced.
