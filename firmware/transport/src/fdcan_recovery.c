#include "oamr/fdcan_recovery.h"

static void increment_sat_u64(uint64_t *value)
{
    if (*value < UINT64_MAX) {
        ++*value;
    }
}

static void clear_recovery(oamr_fdcan_recovery_t *recovery)
{
    *recovery = (oamr_fdcan_recovery_t){0};
}

static bool recovery_is_valid(const oamr_fdcan_recovery_t *recovery)
{
    size_t required_size;

    if (recovery == NULL || !recovery->initialized) {
        return false;
    }

    if (!recovery->events.initialized || recovery->events.storage == NULL ||
        recovery->events.element_size != sizeof(oamr_fdcan_recovery_event_t) ||
        recovery->events.capacity == 0u ||
        recovery->events.count > recovery->events.capacity ||
        recovery->events.head >= recovery->events.capacity ||
        recovery->events.tail >= recovery->events.capacity ||
        recovery->events.element_size > SIZE_MAX / recovery->events.capacity) {
        return false;
    }
    required_size = recovery->events.element_size * recovery->events.capacity;
    if (recovery->events.storage_size < required_size) {
        return false;
    }

    if (recovery->active_attempt_number > recovery->recovery_attempts ||
        recovery->attempts_this_bus_off > recovery->recovery_attempts) {
        return false;
    }

    if (recovery->transport_state == OAMR_FDCAN_TRANSPORT_ONLINE) {
        return !recovery->bus_off && !recovery->attempt_pending &&
               recovery->active_attempt_number == 0u;
    }
    if (recovery->transport_state == OAMR_FDCAN_TRANSPORT_BUS_OFF_WAIT) {
        return !recovery->attempt_pending && recovery->active_attempt_number == 0u;
    }
    if (recovery->transport_state == OAMR_FDCAN_TRANSPORT_RECOVERY_ATTEMPT) {
        return recovery->attempt_pending &&
               recovery->active_attempt_number == recovery->recovery_attempts;
    }
    return false;
}

static oamr_status_t push_event(oamr_fdcan_recovery_t *recovery,
                                uint64_t timestamp_us,
                                oamr_fdcan_recovery_event_type_t type,
                                uint64_t attempt_number,
                                oamr_status_t attempt_result)
{
    oamr_fdcan_recovery_event_t event = {0};
    oamr_status_t status;

    event.timestamp_us = timestamp_us;
    event.attempt_number = attempt_number;
    event.type = type;
    event.attempt_result = attempt_result;
    event.tx_error_count = recovery->tx_error_count;
    event.rx_error_count = recovery->rx_error_count;

    status = oamr_bounded_queue_push(&recovery->events, &event);
    if (status == OAMR_STATUS_OK || status == OAMR_STATUS_OVERFLOW) {
        return status;
    }
    return OAMR_STATUS_ERROR;
}

static oamr_status_t merge_event_status(oamr_status_t current,
                                        oamr_status_t next)
{
    if (current == OAMR_STATUS_ERROR || next == OAMR_STATUS_ERROR) {
        return OAMR_STATUS_ERROR;
    }
    if (current == OAMR_STATUS_OVERFLOW || next == OAMR_STATUS_OVERFLOW) {
        return OAMR_STATUS_OVERFLOW;
    }
    return OAMR_STATUS_OK;
}

oamr_status_t oamr_fdcan_recovery_init(oamr_fdcan_recovery_t *recovery,
                                       void *event_storage,
                                       size_t event_storage_size,
                                       size_t event_capacity)
{
    if (recovery == NULL) {
        return OAMR_STATUS_INVALID;
    }

    clear_recovery(recovery);
    if (oamr_bounded_queue_init(&recovery->events, event_storage,
                                event_storage_size,
                                sizeof(oamr_fdcan_recovery_event_t),
                                event_capacity) != OAMR_STATUS_OK) {
        clear_recovery(recovery);
        return OAMR_STATUS_INVALID;
    }

    oamr_time_validator_init(&recovery->clock);
    recovery->transport_state = OAMR_FDCAN_TRANSPORT_ONLINE;
    recovery->initialized = true;
    return OAMR_STATUS_OK;
}

bool oamr_fdcan2_frame_is_accepted(const oamr_can_frame_t *frame)
{
    return oamr_can_frame_is_valid(frame) &&
           frame->id_type == OAMR_CAN_ID_EXTENDED &&
           frame->id == OAMR_FDCAN2_ACCEPTED_EXTENDED_ID &&
           frame->dlc == OAMR_FDCAN2_ACCEPTED_DLC;
}

oamr_status_t oamr_fdcan_recovery_record_error_status(
    oamr_fdcan_recovery_t *recovery,
    uint64_t timestamp_us,
    bool error_warning,
    bool error_passive,
    bool bus_off,
    uint8_t tx_error_count,
    uint8_t rx_error_count)
{
    const bool entered_warning = recovery != NULL && recovery->initialized &&
                                 error_warning && !recovery->error_warning;
    const bool entered_passive = recovery != NULL && recovery->initialized &&
                                 error_passive && !recovery->error_passive;
    bool new_bus_off_episode;
    oamr_status_t status = OAMR_STATUS_OK;

    if (!recovery_is_valid(recovery) ||
        oamr_time_validate_monotonic(&recovery->clock, timestamp_us) !=
            OAMR_STATUS_OK) {
        return OAMR_STATUS_INVALID;
    }

    new_bus_off_episode = bus_off &&
                          recovery->transport_state == OAMR_FDCAN_TRANSPORT_ONLINE;
    recovery->error_warning = error_warning;
    recovery->error_passive = error_passive;
    recovery->tx_error_count = tx_error_count;
    recovery->rx_error_count = rx_error_count;

    if (entered_warning) {
        increment_sat_u64(&recovery->warning_entries);
        status = merge_event_status(
            status, push_event(recovery, timestamp_us,
                               OAMR_FDCAN_RECOVERY_EVENT_WARNING, 0u,
                               OAMR_STATUS_INVALID));
    }
    if (entered_passive) {
        increment_sat_u64(&recovery->passive_entries);
        status = merge_event_status(
            status, push_event(recovery, timestamp_us,
                               OAMR_FDCAN_RECOVERY_EVENT_PASSIVE, 0u,
                               OAMR_STATUS_INVALID));
    }

    if (new_bus_off_episode) {
        recovery->transport_state = OAMR_FDCAN_TRANSPORT_BUS_OFF_WAIT;
        recovery->wait_started_us = timestamp_us;
        recovery->wait_delay_us = OAMR_FDCAN_RECOVERY_FIRST_DELAY_US;
        recovery->attempts_this_bus_off = 0u;
        increment_sat_u64(&recovery->bus_off_events);
        status = merge_event_status(
            status, push_event(recovery, timestamp_us,
                               OAMR_FDCAN_RECOVERY_EVENT_BUS_OFF, 0u,
                               OAMR_STATUS_INVALID));
    }

    /* Until an attempt succeeds, transport_state remains the recovery latch;
     * bus_off is only the latest hardware observation and may clear early. */
    recovery->bus_off = bus_off;

    return status;
}

oamr_status_t oamr_fdcan_recovery_record_tx_failure(
    oamr_fdcan_recovery_t *recovery,
    uint64_t timestamp_us)
{
    if (!recovery_is_valid(recovery) ||
        oamr_time_validate_monotonic(&recovery->clock, timestamp_us) !=
            OAMR_STATUS_OK) {
        return OAMR_STATUS_INVALID;
    }

    increment_sat_u64(&recovery->tx_failures);
    return OAMR_STATUS_OK;
}

oamr_status_t oamr_fdcan_recovery_record_rx_overflow(
    oamr_fdcan_recovery_t *recovery,
    uint64_t timestamp_us)
{
    if (!recovery_is_valid(recovery) ||
        oamr_time_validate_monotonic(&recovery->clock, timestamp_us) !=
            OAMR_STATUS_OK) {
        return OAMR_STATUS_INVALID;
    }

    increment_sat_u64(&recovery->rx_overflow_events);
    return OAMR_STATUS_OK;
}

oamr_status_t oamr_fdcan_recovery_begin_normal_tx(
    oamr_fdcan_recovery_t *recovery)
{
    if (!recovery_is_valid(recovery)) {
        return OAMR_STATUS_INVALID;
    }
    if (recovery->transport_state != OAMR_FDCAN_TRANSPORT_ONLINE) {
        increment_sat_u64(&recovery->tx_rejected_while_offline);
        return OAMR_STATUS_ERROR;
    }
    return OAMR_STATUS_OK;
}

oamr_status_t oamr_fdcan_recovery_poll(
    oamr_fdcan_recovery_t *recovery,
    uint64_t timestamp_us,
    oamr_fdcan_recovery_attempt_request_t *out_request)
{
    if (out_request != NULL) {
        *out_request = (oamr_fdcan_recovery_attempt_request_t){0};
    }
    if (!recovery_is_valid(recovery) || out_request == NULL ||
        oamr_time_validate_monotonic(&recovery->clock, timestamp_us) !=
            OAMR_STATUS_OK) {
        return OAMR_STATUS_INVALID;
    }

    if (recovery->transport_state == OAMR_FDCAN_TRANSPORT_BUS_OFF_WAIT &&
        timestamp_us - recovery->wait_started_us >= recovery->wait_delay_us) {
        recovery->transport_state = OAMR_FDCAN_TRANSPORT_RECOVERY_ATTEMPT;
        increment_sat_u64(&recovery->recovery_attempts);
        increment_sat_u64(&recovery->attempts_this_bus_off);
        recovery->active_attempt_number = recovery->recovery_attempts;
        recovery->attempt_pending = true;
        out_request->required = true;
        out_request->attempt_number = recovery->active_attempt_number;
    }

    return OAMR_STATUS_OK;
}

oamr_status_t oamr_fdcan_recovery_complete_attempt(
    oamr_fdcan_recovery_t *recovery,
    uint64_t timestamp_us,
    oamr_status_t attempt_result)
{
    uint64_t attempt_number;
    oamr_fdcan_recovery_event_type_t event_type;

    if (!recovery_is_valid(recovery) ||
        recovery->transport_state != OAMR_FDCAN_TRANSPORT_RECOVERY_ATTEMPT) {
        return OAMR_STATUS_INVALID;
    }
    if (oamr_time_validate_monotonic(&recovery->clock, timestamp_us) !=
        OAMR_STATUS_OK) {
        return OAMR_STATUS_INVALID;
    }

    attempt_number = recovery->active_attempt_number;
    recovery->active_attempt_number = 0u;
    recovery->attempt_pending = false;

    if (attempt_result == OAMR_STATUS_OK) {
        recovery->transport_state = OAMR_FDCAN_TRANSPORT_ONLINE;
        recovery->bus_off = false;
        recovery->wait_started_us = 0u;
        recovery->wait_delay_us = 0u;
        recovery->attempts_this_bus_off = 0u;
        increment_sat_u64(&recovery->recovery_successes);
        event_type = OAMR_FDCAN_RECOVERY_EVENT_RECOVERED;
    } else {
        recovery->transport_state = OAMR_FDCAN_TRANSPORT_BUS_OFF_WAIT;
        recovery->bus_off = true;
        recovery->wait_started_us = timestamp_us;
        recovery->wait_delay_us =
            recovery->attempts_this_bus_off == 1u
                ? OAMR_FDCAN_RECOVERY_SECOND_DELAY_US
                : OAMR_FDCAN_RECOVERY_STEADY_DELAY_US;
        increment_sat_u64(&recovery->recovery_failures);
        event_type = OAMR_FDCAN_RECOVERY_EVENT_FAILED;
    }

    return push_event(recovery, timestamp_us, event_type, attempt_number,
                      attempt_result);
}

oamr_status_t oamr_fdcan_recovery_pop_event(
    oamr_fdcan_recovery_t *recovery,
    oamr_fdcan_recovery_event_t *out_event)
{
    if (!recovery_is_valid(recovery) || out_event == NULL) {
        return OAMR_STATUS_INVALID;
    }
    return oamr_bounded_queue_pop(&recovery->events, out_event);
}

oamr_status_t oamr_fdcan_recovery_get_snapshot(
    const oamr_fdcan_recovery_t *recovery,
    oamr_fdcan_recovery_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return OAMR_STATUS_INVALID;
    }
    *out_snapshot = (oamr_fdcan_recovery_snapshot_t){0};
    if (!recovery_is_valid(recovery)) {
        return OAMR_STATUS_INVALID;
    }

    out_snapshot->transport_state = recovery->transport_state;
    out_snapshot->error_warning = recovery->error_warning;
    out_snapshot->error_passive = recovery->error_passive;
    out_snapshot->bus_off = recovery->bus_off;
    out_snapshot->tx_error_count = recovery->tx_error_count;
    out_snapshot->rx_error_count = recovery->rx_error_count;
    out_snapshot->warning_entries = recovery->warning_entries;
    out_snapshot->passive_entries = recovery->passive_entries;
    out_snapshot->bus_off_events = recovery->bus_off_events;
    out_snapshot->tx_failures = recovery->tx_failures;
    out_snapshot->rx_overflow_events = recovery->rx_overflow_events;
    out_snapshot->tx_rejected_while_offline =
        recovery->tx_rejected_while_offline;
    out_snapshot->recovery_attempts = recovery->recovery_attempts;
    out_snapshot->recovery_failures = recovery->recovery_failures;
    out_snapshot->recovery_successes = recovery->recovery_successes;
    out_snapshot->attempts_this_bus_off = recovery->attempts_this_bus_off;
    out_snapshot->event_overflows =
        oamr_bounded_queue_overflow_count(&recovery->events);
    return OAMR_STATUS_OK;
}
