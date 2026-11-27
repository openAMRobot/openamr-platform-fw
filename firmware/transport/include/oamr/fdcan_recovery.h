#ifndef OAMR_FDCAN_RECOVERY_H
#define OAMR_FDCAN_RECOVERY_H

#include "oamr/bounded_queue.h"
#include "oamr/can_frame.h"
#include "oamr/status.h"
#include "oamr/time.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OAMR_FDCAN_RECOVERY_FIRST_DELAY_US UINT64_C(100000)
#define OAMR_FDCAN_RECOVERY_SECOND_DELAY_US UINT64_C(500000)
#define OAMR_FDCAN_RECOVERY_STEADY_DELAY_US UINT64_C(1000000)

#define OAMR_FDCAN2_ACCEPTED_EXTENDED_ID UINT32_C(0x18104001)
#define OAMR_FDCAN2_ACCEPTED_DLC ((uint8_t)8u)

/* This is transport availability only, never motion or safety permission. */
typedef enum {
    OAMR_FDCAN_TRANSPORT_UNINITIALIZED = 0,
    OAMR_FDCAN_TRANSPORT_ONLINE = 1,
    OAMR_FDCAN_TRANSPORT_BUS_OFF_WAIT = 2,
    OAMR_FDCAN_TRANSPORT_RECOVERY_ATTEMPT = 3
} oamr_fdcan_transport_state_t;

typedef enum {
    OAMR_FDCAN_RECOVERY_EVENT_NONE = 0,
    OAMR_FDCAN_RECOVERY_EVENT_WARNING = 1,
    OAMR_FDCAN_RECOVERY_EVENT_PASSIVE = 2,
    OAMR_FDCAN_RECOVERY_EVENT_BUS_OFF = 3,
    OAMR_FDCAN_RECOVERY_EVENT_FAILED = 4,
    OAMR_FDCAN_RECOVERY_EVENT_RECOVERED = 5
} oamr_fdcan_recovery_event_type_t;

/*
 * Diagnostic events are values in caller-owned bounded-queue storage.
 * attempt_number and attempt_result are meaningful only for FAILED/RECOVERED.
 * Other events use attempt_number zero and OAMR_STATUS_INVALID. Saturated
 * lifetime/episode counters can make an attempt number non-unique; this is an
 * identifier, not a freshness or replay guarantee. A full event queue never
 * prevents error accounting or a transport-state transition.
 */
typedef struct {
    uint64_t timestamp_us;
    uint64_t attempt_number;
    oamr_fdcan_recovery_event_type_t type;
    oamr_status_t attempt_result;
    uint8_t tx_error_count;
    uint8_t rx_error_count;
} oamr_fdcan_recovery_event_t;

typedef struct {
    bool required;
    uint64_t attempt_number;
} oamr_fdcan_recovery_attempt_request_t;

typedef struct {
    oamr_fdcan_transport_state_t transport_state;
    bool error_warning;
    bool error_passive;
    bool bus_off;
    uint8_t tx_error_count;
    uint8_t rx_error_count;
    uint64_t warning_entries;
    uint64_t passive_entries;
    uint64_t bus_off_events;
    uint64_t tx_failures;
    uint64_t rx_overflow_events;
    uint64_t tx_rejected_while_offline;
    uint64_t recovery_attempts;
    uint64_t recovery_failures;
    uint64_t recovery_successes;
    uint64_t attempts_this_bus_off;
    uint64_t event_overflows;
} oamr_fdcan_recovery_snapshot_t;

/*
 * All fields are implementation state; callers must not modify them. The
 * structure is public solely to permit static or other caller-owned allocation.
 * Every operation, including event-pop and TX-gate checks, requires external
 * serialization when an ISR or another execution context can share the object.
 */
typedef struct {
    oamr_bounded_queue_t events;
    oamr_time_validator_t clock;
    oamr_fdcan_transport_state_t transport_state;
    bool error_warning;
    bool error_passive;
    bool bus_off;
    bool attempt_pending;
    bool initialized;
    uint8_t tx_error_count;
    uint8_t rx_error_count;
    uint64_t wait_started_us;
    uint64_t wait_delay_us;
    uint64_t active_attempt_number;
    uint64_t warning_entries;
    uint64_t passive_entries;
    uint64_t bus_off_events;
    uint64_t tx_failures;
    uint64_t rx_overflow_events;
    uint64_t tx_rejected_while_offline;
    uint64_t recovery_attempts;
    uint64_t recovery_failures;
    uint64_t recovery_successes;
    uint64_t attempts_this_bus_off;
} oamr_fdcan_recovery_t;

/*
 * Initialize an online transport after the adapter has successfully started
 * the peripheral. event_capacity is an element count, not bytes. Storage must
 * hold at least event_capacity oamr_fdcan_recovery_event_t values and remains
 * caller-owned. Invalid arguments leave a non-NULL recovery zeroed, offline and
 * unable to transmit. Initialization clears all counters and queued events.
 */
oamr_status_t oamr_fdcan_recovery_init(oamr_fdcan_recovery_t *recovery,
                                       void *event_storage,
                                       size_t event_storage_size,
                                       size_t event_capacity);

/*
 * FDCAN2 software post-filter acceptance: a structurally valid Classical CAN
 * data frame, extended ID 0x18104001 and DLC 8 only. The adapter must configure
 * the exact filter to FIFO0, reject all other standard/extended identifiers,
 * and reject unsupported remote/FD frames before conversion. This function
 * cannot verify hardware FIFO routing and does not inspect timestamp, payload
 * or error_flags, decode Daly data, or establish freshness or data validity.
 */
bool oamr_fdcan2_frame_is_accepted(const oamr_can_frame_t *frame);

/*
 * Apply an application-context snapshot of hardware error status. Warning,
 * passive and bus-off entry counters/events occur on false-to-true transitions;
 * TEC/REC are the latest supplied values, not cumulative error-frame counts.
 * A new bus-off while ONLINE immediately disables normal TX and starts the
 * 100 ms wait. Repeated/re-entered bus-off reports during an existing recovery
 * episode do not restart its schedule. Bus-off false alone does not recover TX.
 *
 * Returns INVALID for bad arguments, corrupt state or decreasing time. Equal
 * timestamps are valid. OVERFLOW means all state/accounting was applied but at
 * least one diagnostic event could not be queued. Counters saturate.
 */
oamr_status_t oamr_fdcan_recovery_record_error_status(
    oamr_fdcan_recovery_t *recovery,
    uint64_t timestamp_us,
    bool error_warning,
    bool error_passive,
    bool bus_off,
    uint8_t tx_error_count,
    uint8_t rx_error_count);

/* Count adapter reports, not inferred lost frames or retries. These reports do
 * not alter TX availability by themselves. Time/counter rules match above. */
oamr_status_t oamr_fdcan_recovery_record_tx_failure(
    oamr_fdcan_recovery_t *recovery,
    uint64_t timestamp_us);
oamr_status_t oamr_fdcan_recovery_record_rx_overflow(
    oamr_fdcan_recovery_t *recovery,
    uint64_t timestamp_us);

/*
 * Reserve permission to submit one normal TX operation. This function does not
 * transmit anything. ONLINE returns OK; bus-off wait or an unfinished recovery
 * attempt returns ERROR and saturating-counts the rejection. Warning/passive
 * alone do not inhibit normal TX. UNINITIALIZED/corrupt state returns INVALID.
 * Recovery operations do not use this gate. OK is not motion/safety permission.
 */
oamr_status_t oamr_fdcan_recovery_begin_normal_tx(
    oamr_fdcan_recovery_t *recovery);

/*
 * Nonblocking scheduler poll. At a due boundary it marks exactly one attempt
 * pending and returns required=true with its sequence number. Otherwise it
 * returns required=false; an already-pending attempt is never duplicated.
 * There is no callback, delay, sleep, HAL call or internal retry.
 *
 * On required=true, the application performs its bounded nonblocking adapter
 * attempt and must later call complete_attempt exactly once, including when
 * the adapter cannot attempt recovery. No attempt timeout is selected here.
 * Waits use subtraction, so uint64_t time cannot cause deadline-addition wrap.
 */
oamr_status_t oamr_fdcan_recovery_poll(
    oamr_fdcan_recovery_t *recovery,
    uint64_t timestamp_us,
    oamr_fdcan_recovery_attempt_request_t *out_request);

/*
 * Report the pending adapter result. OK returns transport to ONLINE, permits
 * normal transport TX and queues RECOVERED; it has no other side effect. Every
 * non-OK value is a failed attempt, queues FAILED with that exact result, and
 * schedules the next poll 500 ms after the first failure or 1000 ms after each
 * later failure. The delay starts at this completion timestamp, not poll time.
 * A successful attempt starts a fresh 100 ms schedule for any future bus-off.
 * Calling without a pending attempt returns INVALID without changing state.
 * OVERFLOW reports event loss after the result was fully applied.
 */
oamr_status_t oamr_fdcan_recovery_complete_attempt(
    oamr_fdcan_recovery_t *recovery,
    uint64_t timestamp_us,
    oamr_status_t attempt_result);

/* Pop the oldest diagnostic event. EMPTY leaves out_event untouched. */
oamr_status_t oamr_fdcan_recovery_pop_event(
    oamr_fdcan_recovery_t *recovery,
    oamr_fdcan_recovery_event_t *out_event);

/* Copy a coherent externally serialized snapshot. A non-NULL output is zeroed
 * before INVALID is returned. event_overflows is the bounded queue's count of
 * rejected event pushes, saturating at UINT64_MAX. */
oamr_status_t oamr_fdcan_recovery_get_snapshot(
    const oamr_fdcan_recovery_t *recovery,
    oamr_fdcan_recovery_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_FDCAN_RECOVERY_H */
