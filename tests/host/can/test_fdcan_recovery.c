/* SPDX-License-Identifier: MIT */
#include "test_harness.h"

#include "oamr/fdcan_recovery.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TEST_EVENT_CAPACITY 16u
#define TEST_US_PER_MS UINT64_C(1000)

typedef struct {
    oamr_fdcan_recovery_t recovery;
    oamr_fdcan_recovery_event_t events[TEST_EVENT_CAPACITY];
} recovery_fixture_t;

typedef struct {
    bool motion_permission;
    bool lift_permission;
    bool drive_enable;
    bool brake_release;
    bool contactor_enable;
    uint32_t safety_state_cookie;
} transport_only_sentinel_t;

static const transport_only_sentinel_t sentinel_initial = {
    false, false, false, false, false, UINT32_C(0x51a7e123)
};

static bool sentinel_is_unchanged(const transport_only_sentinel_t *sentinel)
{
    return sentinel->motion_permission == sentinel_initial.motion_permission &&
           sentinel->lift_permission == sentinel_initial.lift_permission &&
           sentinel->drive_enable == sentinel_initial.drive_enable &&
           sentinel->brake_release == sentinel_initial.brake_release &&
           sentinel->contactor_enable == sentinel_initial.contactor_enable &&
           sentinel->safety_state_cookie == sentinel_initial.safety_state_cookie;
}

static void fixture_init(recovery_fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    CHECK(oamr_fdcan_recovery_init(&fixture->recovery, fixture->events,
                                   sizeof(fixture->events),
                                   TEST_EVENT_CAPACITY) == OAMR_STATUS_OK);
}

static void expect_snapshot(const oamr_fdcan_recovery_t *recovery,
                            oamr_fdcan_recovery_snapshot_t *snapshot)
{
    memset(snapshot, 0xa5, sizeof(*snapshot));
    CHECK(oamr_fdcan_recovery_get_snapshot(recovery, snapshot) ==
          OAMR_STATUS_OK);
}

static void expect_event(oamr_fdcan_recovery_t *recovery,
                         oamr_fdcan_recovery_event_type_t type,
                         uint64_t timestamp_us,
                         uint64_t attempt_number,
                         oamr_status_t attempt_result,
                         uint8_t tx_error_count,
                         uint8_t rx_error_count)
{
    oamr_fdcan_recovery_event_t event;

    memset(&event, 0xa5, sizeof(event));
    CHECK(oamr_fdcan_recovery_pop_event(recovery, &event) == OAMR_STATUS_OK);
    CHECK(event.type == type);
    CHECK(event.timestamp_us == timestamp_us);
    CHECK(event.attempt_number == attempt_number);
    CHECK(event.attempt_result == attempt_result);
    CHECK(event.tx_error_count == tx_error_count);
    CHECK(event.rx_error_count == rx_error_count);
}

static void expect_no_event(oamr_fdcan_recovery_t *recovery)
{
    oamr_fdcan_recovery_event_t event;
    oamr_fdcan_recovery_event_t before;

    memset(&event, 0x5a, sizeof(event));
    before = event;
    CHECK(oamr_fdcan_recovery_pop_event(recovery, &event) == OAMR_STATUS_EMPTY);
    CHECK(memcmp(&event, &before, sizeof(event)) == 0);
}

static void expect_poll(oamr_fdcan_recovery_t *recovery, uint64_t timestamp_us,
                        bool required, uint64_t attempt_number)
{
    oamr_fdcan_recovery_attempt_request_t request;

    request.required = !required;
    request.attempt_number = UINT64_MAX;
    CHECK(oamr_fdcan_recovery_poll(recovery, timestamp_us, &request) ==
          OAMR_STATUS_OK);
    CHECK(request.required == required);
    CHECK(request.attempt_number == (required ? attempt_number : 0u));
}

static void expect_bus_off(oamr_fdcan_recovery_t *recovery,
                           uint64_t timestamp_us)
{
    CHECK(oamr_fdcan_recovery_record_error_status(
              recovery, timestamp_us, false, false, true, 0u, 0u) ==
          OAMR_STATUS_OK);
}

static void initialization_rejects_invalid_arguments(void)
{
    recovery_fixture_t fixture;
    oamr_fdcan_recovery_snapshot_t snapshot;

    memset(&fixture, 0xa5, sizeof(fixture));
    CHECK(oamr_fdcan_recovery_init(NULL, fixture.events,
                                   sizeof(fixture.events), 1u) ==
          OAMR_STATUS_INVALID);

    CHECK(oamr_fdcan_recovery_init(&fixture.recovery, NULL,
                                   sizeof(fixture.events), 1u) ==
          OAMR_STATUS_INVALID);
    CHECK(!fixture.recovery.initialized);
    CHECK(fixture.recovery.transport_state ==
          OAMR_FDCAN_TRANSPORT_UNINITIALIZED);
    CHECK(oamr_fdcan_recovery_begin_normal_tx(&fixture.recovery) ==
          OAMR_STATUS_INVALID);

    memset(&fixture.recovery, 0xa5, sizeof(fixture.recovery));
    CHECK(oamr_fdcan_recovery_init(&fixture.recovery, fixture.events, 0u, 1u) ==
          OAMR_STATUS_INVALID);
    CHECK(!fixture.recovery.initialized);
    CHECK(oamr_fdcan_recovery_begin_normal_tx(&fixture.recovery) ==
          OAMR_STATUS_INVALID);

    memset(&fixture.recovery, 0xa5, sizeof(fixture.recovery));
    CHECK(oamr_fdcan_recovery_init(&fixture.recovery, fixture.events,
                                   sizeof(fixture.events), 0u) ==
          OAMR_STATUS_INVALID);
    CHECK(!fixture.recovery.initialized);
    CHECK(oamr_fdcan_recovery_begin_normal_tx(&fixture.recovery) ==
          OAMR_STATUS_INVALID);

    memset(&fixture.recovery, 0xa5, sizeof(fixture.recovery));
    CHECK(oamr_fdcan_recovery_init(
              &fixture.recovery, fixture.events, SIZE_MAX,
              (SIZE_MAX / sizeof(oamr_fdcan_recovery_event_t)) + 1u) ==
          OAMR_STATUS_INVALID);
    CHECK(!fixture.recovery.initialized);
    CHECK(oamr_fdcan_recovery_begin_normal_tx(&fixture.recovery) ==
          OAMR_STATUS_INVALID);

    memset(&fixture.recovery, 0xa5, sizeof(fixture.recovery));
    CHECK(oamr_fdcan_recovery_init(&fixture.recovery, fixture.events,
                                   sizeof(oamr_fdcan_recovery_event_t) - 1u,
                                   1u) == OAMR_STATUS_INVALID);
    CHECK(!fixture.recovery.initialized);
    CHECK(oamr_fdcan_recovery_begin_normal_tx(&fixture.recovery) ==
          OAMR_STATUS_INVALID);

    fixture_init(&fixture);
    CHECK(oamr_fdcan_recovery_begin_normal_tx(&fixture.recovery) ==
          OAMR_STATUS_OK);
    CHECK(oamr_fdcan_recovery_record_error_status(&fixture.recovery, 0u,
                                                  false, false, false,
                                                  0u, 0u) == OAMR_STATUS_OK);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.transport_state == OAMR_FDCAN_TRANSPORT_ONLINE);
    CHECK(snapshot.warning_entries == 0u);
    CHECK(snapshot.passive_entries == 0u);
    CHECK(snapshot.bus_off_events == 0u);
    CHECK(snapshot.recovery_attempts == 0u);
    CHECK(oamr_bounded_queue_count(&fixture.recovery.events) == 0u);

    memset(&snapshot, 0xa5, sizeof(snapshot));
    CHECK(oamr_fdcan_recovery_get_snapshot(NULL, &snapshot) ==
          OAMR_STATUS_INVALID);
    {
        oamr_fdcan_recovery_snapshot_t zero = {0};
        CHECK(memcmp(&snapshot, &zero, sizeof(snapshot)) == 0);
    }
    CHECK(oamr_fdcan_recovery_get_snapshot(&fixture.recovery, NULL) ==
          OAMR_STATUS_INVALID);
}

static void invalid_calls_preserve_initialized_state(void)
{
    recovery_fixture_t fixture;
    oamr_fdcan_recovery_snapshot_t before;
    oamr_fdcan_recovery_snapshot_t after;
    oamr_fdcan_recovery_event_t event;
    oamr_fdcan_recovery_event_t untouched;
    oamr_fdcan_recovery_attempt_request_t request;

    fixture_init(&fixture);
    CHECK(oamr_fdcan_recovery_record_tx_failure(&fixture.recovery, 10u) ==
          OAMR_STATUS_OK);
    CHECK(oamr_fdcan_recovery_record_rx_overflow(&fixture.recovery, 10u) ==
          OAMR_STATUS_OK);
    expect_snapshot(&fixture.recovery, &before);

    CHECK(oamr_fdcan_recovery_record_error_status(NULL, 10u, false, false,
                                                  false, 0u, 0u) ==
          OAMR_STATUS_INVALID);
    CHECK(oamr_fdcan_recovery_record_tx_failure(NULL, 10u) ==
          OAMR_STATUS_INVALID);
    CHECK(oamr_fdcan_recovery_record_rx_overflow(NULL, 10u) ==
          OAMR_STATUS_INVALID);
    CHECK(oamr_fdcan_recovery_begin_normal_tx(NULL) == OAMR_STATUS_INVALID);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery, 10u,
                                               OAMR_STATUS_OK) ==
          OAMR_STATUS_INVALID);
    CHECK(oamr_fdcan_recovery_pop_event(&fixture.recovery, NULL) ==
          OAMR_STATUS_INVALID);
    CHECK(oamr_fdcan_recovery_pop_event(NULL, &event) == OAMR_STATUS_INVALID);

    memset(&untouched, 0x6c, sizeof(untouched));
    event = untouched;
    CHECK(oamr_fdcan_recovery_pop_event(&fixture.recovery, &event) ==
          OAMR_STATUS_EMPTY);
    CHECK(memcmp(&event, &untouched, sizeof(event)) == 0);

    request.required = true;
    request.attempt_number = UINT64_MAX;
    CHECK(oamr_fdcan_recovery_poll(&fixture.recovery, 10u, NULL) ==
          OAMR_STATUS_INVALID);
    expect_snapshot(&fixture.recovery, &after);
    CHECK(memcmp(&before, &after, sizeof(before)) == 0);
    (void)request;
}

static void initialization_clears_previous_episode_and_events(void)
{
    recovery_fixture_t fixture;
    oamr_fdcan_recovery_snapshot_t snapshot;

    fixture_init(&fixture);
    expect_bus_off(&fixture.recovery, 100u);
    CHECK(oamr_fdcan_recovery_record_tx_failure(&fixture.recovery, 100u) ==
          OAMR_STATUS_OK);
    CHECK(oamr_fdcan_recovery_record_rx_overflow(&fixture.recovery, 100u) ==
          OAMR_STATUS_OK);
    expect_poll(&fixture.recovery, UINT64_C(100100), true, 1u);
    CHECK(oamr_fdcan_recovery_init(&fixture.recovery, fixture.events,
                                   sizeof(fixture.events), 1u) ==
          OAMR_STATUS_OK);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.transport_state == OAMR_FDCAN_TRANSPORT_ONLINE);
    CHECK(snapshot.bus_off_events == 0u);
    CHECK(snapshot.tx_failures == 0u);
    CHECK(snapshot.rx_overflow_events == 0u);
    CHECK(snapshot.recovery_attempts == 0u);
    CHECK(snapshot.event_overflows == 0u);
    expect_no_event(&fixture.recovery);
    CHECK(oamr_bounded_queue_capacity(&fixture.recovery.events) == 1u);
}

static void fdcan2_filter_accepts_only_exact_extended_dlc8(void)
{
    oamr_can_frame_t frame = {0};

    CHECK(!oamr_fdcan2_frame_is_accepted(NULL));
    CHECK(OAMR_FDCAN2_ACCEPTED_EXTENDED_ID == UINT32_C(0x18104001));
    CHECK(OAMR_FDCAN2_ACCEPTED_DLC == 8u);

    frame.id_type = OAMR_CAN_ID_EXTENDED;
    frame.id = OAMR_FDCAN2_ACCEPTED_EXTENDED_ID;
    frame.dlc = OAMR_FDCAN2_ACCEPTED_DLC;
    CHECK(oamr_can_frame_is_valid(&frame));
    CHECK(oamr_fdcan2_frame_is_accepted(&frame));

    frame.id = UINT32_C(0x18104000);
    CHECK(!oamr_fdcan2_frame_is_accepted(&frame));
    frame.id = UINT32_C(0x18104002);
    CHECK(!oamr_fdcan2_frame_is_accepted(&frame));
    frame.id = UINT32_C(0x08104001);
    CHECK(!oamr_fdcan2_frame_is_accepted(&frame));
    frame.id = OAMR_CAN_EXTENDED_ID_MAX;
    CHECK(!oamr_fdcan2_frame_is_accepted(&frame));
    frame.id = OAMR_CAN_EXTENDED_ID_MAX + UINT32_C(1);
    CHECK(!oamr_can_frame_is_valid(&frame));
    CHECK(!oamr_fdcan2_frame_is_accepted(&frame));

    frame.id = OAMR_FDCAN2_ACCEPTED_EXTENDED_ID;
    frame.id_type = OAMR_CAN_ID_STANDARD;
    CHECK(!oamr_can_frame_is_valid(&frame));
    CHECK(!oamr_fdcan2_frame_is_accepted(&frame));
    frame.id = UINT32_C(0x401);
    CHECK(oamr_can_frame_is_valid(&frame));
    CHECK(!oamr_fdcan2_frame_is_accepted(&frame));
    frame.id = OAMR_CAN_STANDARD_ID_MAX + UINT32_C(1);
    CHECK(!oamr_can_frame_is_valid(&frame));
    CHECK(!oamr_fdcan2_frame_is_accepted(&frame));

    frame.id_type = (oamr_can_id_type_t)2;
    frame.id = OAMR_FDCAN2_ACCEPTED_EXTENDED_ID;
    CHECK(!oamr_can_frame_is_valid(&frame));
    CHECK(!oamr_fdcan2_frame_is_accepted(&frame));

    frame.id_type = OAMR_CAN_ID_EXTENDED;
    frame.dlc = 0u;
    CHECK(!oamr_fdcan2_frame_is_accepted(&frame));
    frame.dlc = 7u;
    CHECK(!oamr_fdcan2_frame_is_accepted(&frame));
    frame.dlc = 9u;
    CHECK(!oamr_can_frame_is_valid(&frame));
    CHECK(!oamr_fdcan2_frame_is_accepted(&frame));
    frame.dlc = UINT8_MAX;
    CHECK(!oamr_fdcan2_frame_is_accepted(&frame));
}

static void fdcan2_filter_ignores_metadata_and_payload(void)
{
    oamr_can_frame_t frame = {0};
    size_t index;

    frame.timestamp_us = UINT64_MAX;
    frame.id = OAMR_FDCAN2_ACCEPTED_EXTENDED_ID;
    frame.id_type = OAMR_CAN_ID_EXTENDED;
    frame.error_flags = (oamr_can_error_t)(OAMR_CAN_ERROR_BIT |
                                           OAMR_CAN_ERROR_STUFF |
                                           OAMR_CAN_ERROR_CRC |
                                           OAMR_CAN_ERROR_FORM |
                                           OAMR_CAN_ERROR_ACK |
                                           OAMR_CAN_ERROR_OVERFLOW |
                                           OAMR_CAN_ERROR_BUS_OFF |
                                           OAMR_CAN_ERROR_OTHER);
    frame.dlc = OAMR_FDCAN2_ACCEPTED_DLC;
    for (index = 0u; index < sizeof(frame.data); ++index) {
        frame.data[index] = (uint8_t)(0xf0u + (uint8_t)index);
    }

    CHECK(oamr_can_frame_is_valid(&frame));
    CHECK(oamr_fdcan2_frame_is_accepted(&frame));
    CHECK(frame.timestamp_us == UINT64_MAX);
    CHECK(frame.error_flags != OAMR_CAN_ERROR_NONE);
    CHECK(frame.data[0] == 0xf0u);
    CHECK(frame.data[7] == 0xf7u);
    /* Acceptance here is structural transport filtering, not freshness,
     * protocol validity, battery health, or permission to operate. */
}

static void warning_passive_and_bus_off_entries_are_edge_counted(void)
{
    recovery_fixture_t fixture;
    oamr_fdcan_recovery_snapshot_t snapshot;

    fixture_init(&fixture);
    CHECK(oamr_fdcan_recovery_record_error_status(&fixture.recovery, 0u,
                                                  true, true, true,
                                                  0u, 255u) == OAMR_STATUS_OK);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.error_warning);
    CHECK(snapshot.error_passive);
    CHECK(snapshot.bus_off);
    CHECK(snapshot.warning_entries == 1u);
    CHECK(snapshot.passive_entries == 1u);
    CHECK(snapshot.bus_off_events == 1u);
    CHECK(snapshot.tx_error_count == 0u);
    CHECK(snapshot.rx_error_count == 255u);
    CHECK(snapshot.transport_state == OAMR_FDCAN_TRANSPORT_BUS_OFF_WAIT);
    CHECK(oamr_bounded_queue_count(&fixture.recovery.events) == 3u);

    CHECK(oamr_fdcan_recovery_record_error_status(&fixture.recovery, 1u,
                                                  true, true, true,
                                                  255u, 0u) == OAMR_STATUS_OK);
    CHECK(oamr_fdcan_recovery_record_error_status(&fixture.recovery, 2u,
                                                  false, false, false,
                                                  0u, 0u) == OAMR_STATUS_OK);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.warning_entries == 1u);
    CHECK(snapshot.passive_entries == 1u);
    CHECK(snapshot.bus_off_events == 1u);
    CHECK(!snapshot.error_warning);
    CHECK(!snapshot.error_passive);
    CHECK(!snapshot.bus_off);
    CHECK(snapshot.transport_state == OAMR_FDCAN_TRANSPORT_BUS_OFF_WAIT);

    CHECK(oamr_fdcan_recovery_record_error_status(&fixture.recovery, 3u,
                                                  true, true, false,
                                                  7u, 9u) == OAMR_STATUS_OK);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.warning_entries == 2u);
    CHECK(snapshot.passive_entries == 2u);
    CHECK(snapshot.bus_off_events == 1u);
    CHECK(snapshot.tx_error_count == 7u);
    CHECK(snapshot.rx_error_count == 9u);

    expect_event(&fixture.recovery, OAMR_FDCAN_RECOVERY_EVENT_WARNING,
                 0u, 0u, OAMR_STATUS_INVALID, 0u, 255u);
    expect_event(&fixture.recovery, OAMR_FDCAN_RECOVERY_EVENT_PASSIVE,
                 0u, 0u, OAMR_STATUS_INVALID, 0u, 255u);
    expect_event(&fixture.recovery, OAMR_FDCAN_RECOVERY_EVENT_BUS_OFF,
                 0u, 0u, OAMR_STATUS_INVALID, 0u, 255u);
    expect_event(&fixture.recovery, OAMR_FDCAN_RECOVERY_EVENT_WARNING,
                 3u, 0u, OAMR_STATUS_INVALID, 7u, 9u);
    expect_event(&fixture.recovery, OAMR_FDCAN_RECOVERY_EVENT_PASSIVE,
                 3u, 0u, OAMR_STATUS_INVALID, 7u, 9u);
    expect_no_event(&fixture.recovery);
}

static void tx_failure_and_rx_overflow_are_independent(void)
{
    recovery_fixture_t fixture;
    oamr_fdcan_recovery_snapshot_t snapshot;

    fixture_init(&fixture);
    CHECK(oamr_fdcan_recovery_record_tx_failure(&fixture.recovery, 0u) ==
          OAMR_STATUS_OK);
    CHECK(oamr_fdcan_recovery_record_rx_overflow(&fixture.recovery, 1u) ==
          OAMR_STATUS_OK);
    CHECK(oamr_fdcan_recovery_record_tx_failure(&fixture.recovery, 1u) ==
          OAMR_STATUS_OK);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.tx_failures == 2u);
    CHECK(snapshot.rx_overflow_events == 1u);
    CHECK(snapshot.bus_off_events == 0u);
    CHECK(snapshot.transport_state == OAMR_FDCAN_TRANSPORT_ONLINE);
    CHECK(oamr_fdcan_recovery_begin_normal_tx(&fixture.recovery) ==
          OAMR_STATUS_OK);
    expect_no_event(&fixture.recovery);
}

static void warning_and_passive_do_not_gate_normal_tx(void)
{
    recovery_fixture_t fixture;
    oamr_fdcan_recovery_snapshot_t snapshot;

    fixture_init(&fixture);
    CHECK(oamr_fdcan_recovery_record_error_status(&fixture.recovery, 100u,
                                                  true, true, false,
                                                  96u, 97u) == OAMR_STATUS_OK);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.error_warning);
    CHECK(snapshot.error_passive);
    CHECK(snapshot.transport_state == OAMR_FDCAN_TRANSPORT_ONLINE);
    CHECK(snapshot.tx_rejected_while_offline == 0u);
    CHECK(oamr_fdcan_recovery_begin_normal_tx(&fixture.recovery) ==
          OAMR_STATUS_OK);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.tx_rejected_while_offline == 0u);
}

static void first_deadline_is_exact_and_pending_is_not_duplicated(void)
{
    recovery_fixture_t fixture;
    oamr_fdcan_recovery_snapshot_t snapshot;

    CHECK(OAMR_FDCAN_RECOVERY_FIRST_DELAY_US == UINT64_C(100000));
    CHECK(OAMR_FDCAN_RECOVERY_SECOND_DELAY_US == UINT64_C(500000));
    CHECK(OAMR_FDCAN_RECOVERY_STEADY_DELAY_US == UINT64_C(1000000));
    fixture_init(&fixture);
    expect_bus_off(&fixture.recovery, 0u);
    CHECK(oamr_fdcan_recovery_begin_normal_tx(&fixture.recovery) ==
          OAMR_STATUS_ERROR);
    CHECK(oamr_fdcan_recovery_begin_normal_tx(&fixture.recovery) ==
          OAMR_STATUS_ERROR);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.tx_rejected_while_offline == 2u);
    CHECK(snapshot.recovery_attempts == 0u);

    expect_poll(&fixture.recovery, 0u, false, 0u);
    expect_poll(&fixture.recovery, UINT64_C(99999), false, 0u);
    expect_poll(&fixture.recovery, UINT64_C(100000), true, 1u);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.transport_state == OAMR_FDCAN_TRANSPORT_RECOVERY_ATTEMPT);
    CHECK(snapshot.recovery_attempts == 1u);
    CHECK(snapshot.attempts_this_bus_off == 1u);
    CHECK(oamr_fdcan_recovery_begin_normal_tx(&fixture.recovery) ==
          OAMR_STATUS_ERROR);
    expect_poll(&fixture.recovery, UINT64_C(100000), false, 0u);
    expect_poll(&fixture.recovery, UINT64_C(900000), false, 0u);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.recovery_attempts == 1u);
    CHECK(snapshot.tx_rejected_while_offline == 3u);
}

static void late_poll_does_not_create_a_catch_up_burst(void)
{
    recovery_fixture_t fixture;
    oamr_fdcan_recovery_snapshot_t snapshot;

    fixture_init(&fixture);
    expect_bus_off(&fixture.recovery, UINT64_C(1000));
    expect_poll(&fixture.recovery, UINT64_C(10000000), true, 1u);
    expect_poll(&fixture.recovery, UINT64_C(20000000), false, 0u);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.recovery_attempts == 1u);
    CHECK(snapshot.attempts_this_bus_off == 1u);
    CHECK(snapshot.recovery_failures == 0u);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery,
                                               UINT64_C(20000000),
                                               OAMR_STATUS_TIMEOUT) ==
          OAMR_STATUS_OK);
    expect_poll(&fixture.recovery, UINT64_C(20000001), false, 0u);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.recovery_attempts == 1u);
    CHECK(snapshot.recovery_failures == 1u);
}

static void run_failed_result_schedule(oamr_status_t failed_result)
{
    recovery_fixture_t fixture;
    oamr_fdcan_recovery_snapshot_t snapshot;
    const uint64_t first_poll = UINT64_C(100000);
    const uint64_t first_complete = UINT64_C(123456);
    const uint64_t second_due = first_complete +
                                OAMR_FDCAN_RECOVERY_SECOND_DELAY_US;
    const uint64_t second_complete = second_due + UINT64_C(777);
    const uint64_t third_due = second_complete +
                               OAMR_FDCAN_RECOVERY_STEADY_DELAY_US;
    const uint64_t third_complete = third_due + UINT64_C(888);
    const uint64_t fourth_due = third_complete +
                                OAMR_FDCAN_RECOVERY_STEADY_DELAY_US;

    fixture_init(&fixture);
    expect_bus_off(&fixture.recovery, 0u);
    expect_event(&fixture.recovery, OAMR_FDCAN_RECOVERY_EVENT_BUS_OFF,
                 0u, 0u, OAMR_STATUS_INVALID, 0u, 0u);
    expect_poll(&fixture.recovery, first_poll, true, 1u);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery,
                                               first_complete,
                                               failed_result) ==
          OAMR_STATUS_OK);
    expect_event(&fixture.recovery, OAMR_FDCAN_RECOVERY_EVENT_FAILED,
                 first_complete, 1u, failed_result, 0u, 0u);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.transport_state == OAMR_FDCAN_TRANSPORT_BUS_OFF_WAIT);
    CHECK(snapshot.recovery_failures == 1u);
    CHECK(snapshot.recovery_attempts == 1u);
    CHECK(snapshot.bus_off);

    expect_poll(&fixture.recovery, second_due - 1u, false, 0u);
    expect_poll(&fixture.recovery, second_due, true, 2u);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery,
                                               second_complete,
                                               OAMR_STATUS_ERROR) ==
          OAMR_STATUS_OK);
    expect_event(&fixture.recovery, OAMR_FDCAN_RECOVERY_EVENT_FAILED,
                 second_complete, 2u, OAMR_STATUS_ERROR, 0u, 0u);

    expect_poll(&fixture.recovery, third_due - 1u, false, 0u);
    expect_poll(&fixture.recovery, third_due, true, 3u);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery,
                                               third_complete,
                                               OAMR_STATUS_INVALID) ==
          OAMR_STATUS_OK);
    expect_event(&fixture.recovery, OAMR_FDCAN_RECOVERY_EVENT_FAILED,
                 third_complete, 3u, OAMR_STATUS_INVALID, 0u, 0u);

    expect_poll(&fixture.recovery, fourth_due - 1u, false, 0u);
    expect_poll(&fixture.recovery, fourth_due, true, 4u);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.recovery_attempts == 4u);
    CHECK(snapshot.attempts_this_bus_off == 4u);
    CHECK(snapshot.recovery_failures == 3u);
    CHECK(snapshot.recovery_successes == 0u);
    expect_no_event(&fixture.recovery);
}

static void failed_attempt_schedules_use_each_exact_non_ok_result(void)
{
    run_failed_result_schedule(OAMR_STATUS_INVALID);
    run_failed_result_schedule(OAMR_STATUS_EMPTY);
    run_failed_result_schedule(OAMR_STATUS_STALE);
}

static void failed_attempt_schedules_timeout_overflow_and_error(void)
{
    run_failed_result_schedule(OAMR_STATUS_TIMEOUT);
    run_failed_result_schedule(OAMR_STATUS_OVERFLOW);
    run_failed_result_schedule(OAMR_STATUS_ERROR);
}

static void successful_completion_restores_transport_only_once(void)
{
    recovery_fixture_t fixture;
    oamr_fdcan_recovery_snapshot_t before;
    oamr_fdcan_recovery_snapshot_t after;

    fixture_init(&fixture);
    expect_bus_off(&fixture.recovery, 10u);
    expect_event(&fixture.recovery, OAMR_FDCAN_RECOVERY_EVENT_BUS_OFF,
                 10u, 0u, OAMR_STATUS_INVALID, 0u, 0u);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery, 20u,
                                               OAMR_STATUS_OK) ==
          OAMR_STATUS_INVALID);
    expect_poll(&fixture.recovery, UINT64_C(100010), true, 1u);
    expect_snapshot(&fixture.recovery, &before);
    CHECK(before.transport_state == OAMR_FDCAN_TRANSPORT_RECOVERY_ATTEMPT);
    CHECK(oamr_fdcan_recovery_begin_normal_tx(&fixture.recovery) ==
          OAMR_STATUS_ERROR);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery,
                                               UINT64_C(100020),
                                               OAMR_STATUS_OK) ==
          OAMR_STATUS_OK);
    expect_event(&fixture.recovery, OAMR_FDCAN_RECOVERY_EVENT_RECOVERED,
                 UINT64_C(100020), 1u, OAMR_STATUS_OK, 0u, 0u);
    expect_no_event(&fixture.recovery);
    expect_snapshot(&fixture.recovery, &after);
    CHECK(after.transport_state == OAMR_FDCAN_TRANSPORT_ONLINE);
    CHECK(!after.bus_off);
    CHECK(after.recovery_attempts == 1u);
    CHECK(after.recovery_successes == 1u);
    CHECK(after.recovery_failures == 0u);
    CHECK(after.attempts_this_bus_off == 0u);
    CHECK(oamr_fdcan_recovery_begin_normal_tx(&fixture.recovery) ==
          OAMR_STATUS_OK);

    expect_snapshot(&fixture.recovery, &before);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery,
                                               UINT64_C(100020),
                                               OAMR_STATUS_OK) ==
          OAMR_STATUS_INVALID);
    expect_poll(&fixture.recovery, UINT64_C(100020), false, 0u);
    expect_snapshot(&fixture.recovery, &after);
    CHECK(memcmp(&before, &after, sizeof(before)) == 0);
}

static void repeated_reports_do_not_reset_wait_or_pending_attempt(void)
{
    recovery_fixture_t fixture;
    oamr_fdcan_recovery_snapshot_t snapshot;

    fixture_init(&fixture);
    expect_bus_off(&fixture.recovery, UINT64_C(1000));
    CHECK(oamr_fdcan_recovery_record_error_status(&fixture.recovery,
                                                  UINT64_C(50000),
                                                  true, true, true,
                                                  100u, 101u) ==
          OAMR_STATUS_OK);
    CHECK(oamr_fdcan_recovery_record_error_status(&fixture.recovery,
                                                  UINT64_C(90000),
                                                  false, false, false,
                                                  0u, 0u) == OAMR_STATUS_OK);
    expect_poll(&fixture.recovery, UINT64_C(100999), false, 0u);
    expect_poll(&fixture.recovery, UINT64_C(101000), true, 1u);
    CHECK(oamr_fdcan_recovery_record_error_status(&fixture.recovery,
                                                  UINT64_C(101001),
                                                  true, true, true,
                                                  200u, 201u) ==
          OAMR_STATUS_OK);
    CHECK(oamr_fdcan_recovery_record_error_status(&fixture.recovery,
                                                  UINT64_C(101002),
                                                  false, false, false,
                                                  0u, 0u) == OAMR_STATUS_OK);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.bus_off_events == 1u);
    CHECK(snapshot.recovery_attempts == 1u);
    CHECK(snapshot.warning_entries == 2u);
    CHECK(snapshot.passive_entries == 2u);
    CHECK(!snapshot.bus_off);
    CHECK(snapshot.transport_state == OAMR_FDCAN_TRANSPORT_RECOVERY_ATTEMPT);
    CHECK(oamr_fdcan_recovery_begin_normal_tx(&fixture.recovery) ==
          OAMR_STATUS_ERROR);
    expect_poll(&fixture.recovery, UINT64_C(2000000), false, 0u);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery,
                                               UINT64_C(2000000),
                                               OAMR_STATUS_TIMEOUT) ==
          OAMR_STATUS_OK);
    expect_poll(&fixture.recovery, UINT64_C(2499999), false, 0u);
    expect_poll(&fixture.recovery, UINT64_C(2500000), true, 2u);
}

static void new_bus_off_after_success_restarts_first_delay(void)
{
    recovery_fixture_t fixture;
    oamr_fdcan_recovery_snapshot_t snapshot;

    fixture_init(&fixture);
    expect_bus_off(&fixture.recovery, 0u);
    expect_poll(&fixture.recovery, UINT64_C(100000), true, 1u);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery,
                                               UINT64_C(100001),
                                               OAMR_STATUS_OK) ==
          OAMR_STATUS_OK);
    expect_bus_off(&fixture.recovery, UINT64_C(200000));
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.bus_off_events == 2u);
    CHECK(snapshot.recovery_attempts == 1u);
    CHECK(snapshot.recovery_successes == 1u);
    CHECK(snapshot.attempts_this_bus_off == 0u);
    expect_poll(&fixture.recovery, UINT64_C(299999), false, 0u);
    expect_poll(&fixture.recovery, UINT64_C(300000), true, 2u);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.attempts_this_bus_off == 1u);
}

static void equal_time_and_regressions_preserve_each_operation(void)
{
    recovery_fixture_t fixture;
    oamr_fdcan_recovery_snapshot_t before;
    oamr_fdcan_recovery_snapshot_t after;
    oamr_fdcan_recovery_attempt_request_t request;

    fixture_init(&fixture);
    expect_bus_off(&fixture.recovery, UINT64_C(1000));
    CHECK(oamr_fdcan_recovery_record_tx_failure(&fixture.recovery,
                                                UINT64_C(1000)) ==
          OAMR_STATUS_OK);
    CHECK(oamr_fdcan_recovery_record_rx_overflow(&fixture.recovery,
                                                 UINT64_C(1000)) ==
          OAMR_STATUS_OK);
    expect_poll(&fixture.recovery, UINT64_C(1000), false, 0u);
    expect_snapshot(&fixture.recovery, &before);

    CHECK(oamr_fdcan_recovery_record_error_status(&fixture.recovery, 999u,
                                                  false, false, false,
                                                  0u, 0u) == OAMR_STATUS_INVALID);
    CHECK(oamr_fdcan_recovery_record_tx_failure(&fixture.recovery, 999u) ==
          OAMR_STATUS_INVALID);
    CHECK(oamr_fdcan_recovery_record_rx_overflow(&fixture.recovery, 999u) ==
          OAMR_STATUS_INVALID);
    request.required = true;
    request.attempt_number = UINT64_MAX;
    CHECK(oamr_fdcan_recovery_poll(&fixture.recovery, 999u, &request) ==
          OAMR_STATUS_INVALID);
    CHECK(!request.required);
    CHECK(request.attempt_number == 0u);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery, 999u,
                                               OAMR_STATUS_OK) ==
          OAMR_STATUS_INVALID);
    expect_snapshot(&fixture.recovery, &after);
    CHECK(memcmp(&before, &after, sizeof(before)) == 0);
    CHECK(fixture.recovery.clock.last_timestamp_us == UINT64_C(1000));

    expect_poll(&fixture.recovery, UINT64_C(1000), false, 0u);
    expect_poll(&fixture.recovery, UINT64_C(101000), true, 1u);
    expect_snapshot(&fixture.recovery, &before);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery,
                                               UINT64_C(100999),
                                               OAMR_STATUS_OK) ==
          OAMR_STATUS_INVALID);
    expect_snapshot(&fixture.recovery, &after);
    CHECK(memcmp(&before, &after, sizeof(before)) == 0);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery,
                                               UINT64_C(101000),
                                               OAMR_STATUS_TIMEOUT) ==
          OAMR_STATUS_OK);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery,
                                               UINT64_C(101000),
                                               OAMR_STATUS_TIMEOUT) ==
          OAMR_STATUS_INVALID);
}

static void elapsed_deadlines_above_32_bits_do_not_wrap(void)
{
    recovery_fixture_t fixture;
    const uint64_t start = UINT64_C(5000000000);

    fixture_init(&fixture);
    expect_bus_off(&fixture.recovery, start);
    expect_poll(&fixture.recovery,
                start + OAMR_FDCAN_RECOVERY_FIRST_DELAY_US - 1u,
                false, 0u);
    expect_poll(&fixture.recovery,
                start + OAMR_FDCAN_RECOVERY_FIRST_DELAY_US,
                true, 1u);
    CHECK(oamr_fdcan_recovery_complete_attempt(
              &fixture.recovery,
              start + OAMR_FDCAN_RECOVERY_FIRST_DELAY_US + UINT32_MAX,
              OAMR_STATUS_ERROR) == OAMR_STATUS_OK);
    expect_poll(&fixture.recovery,
                start + OAMR_FDCAN_RECOVERY_FIRST_DELAY_US + UINT32_MAX +
                    OAMR_FDCAN_RECOVERY_SECOND_DELAY_US - 1u,
                false, 0u);
    expect_poll(&fixture.recovery,
                start + OAMR_FDCAN_RECOVERY_FIRST_DELAY_US + UINT32_MAX +
                    OAMR_FDCAN_RECOVERY_SECOND_DELAY_US,
                true, 2u);
}

static void uint64_max_boundaries_have_no_wrapped_early_attempt(void)
{
    recovery_fixture_t fixture;
    oamr_fdcan_recovery_snapshot_t snapshot;
    const uint64_t bus_off_time = UINT64_MAX - UINT64_C(50000);

    fixture_init(&fixture);
    expect_bus_off(&fixture.recovery, bus_off_time);
    expect_poll(&fixture.recovery, UINT64_MAX - 1u, false, 0u);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.recovery_attempts == 0u);
    expect_poll(&fixture.recovery, UINT64_MAX, false, 0u);

    fixture_init(&fixture);
    expect_bus_off(&fixture.recovery, UINT64_MAX -
                                      OAMR_FDCAN_RECOVERY_FIRST_DELAY_US);
    expect_poll(&fixture.recovery, UINT64_MAX - 1u, false, 0u);
    expect_poll(&fixture.recovery, UINT64_MAX, true, 1u);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery, UINT64_MAX,
                                               OAMR_STATUS_OK) ==
          OAMR_STATUS_OK);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.transport_state == OAMR_FDCAN_TRANSPORT_ONLINE);
    CHECK(snapshot.recovery_successes == 1u);

    fixture_init(&fixture);
    expect_bus_off(&fixture.recovery, UINT64_MAX);
    expect_poll(&fixture.recovery, UINT64_MAX, false, 0u);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery, UINT64_MAX,
                                               OAMR_STATUS_ERROR) ==
          OAMR_STATUS_INVALID);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.recovery_attempts == 0u);
    CHECK(snapshot.transport_state == OAMR_FDCAN_TRANSPORT_BUS_OFF_WAIT);
}

static void event_queue_overflow_preserves_state_and_fifo_order(void)
{
    recovery_fixture_t fixture;
    oamr_fdcan_recovery_snapshot_t snapshot;
    oamr_fdcan_recovery_event_t event;
    oamr_fdcan_recovery_event_t untouched;

    memset(&fixture, 0, sizeof(fixture));
    CHECK(oamr_fdcan_recovery_init(&fixture.recovery, fixture.events,
                                   sizeof(fixture.events), 1u) ==
          OAMR_STATUS_OK);
    CHECK(oamr_fdcan_recovery_record_error_status(&fixture.recovery, 10u,
                                                  true, false, false,
                                                  1u, 2u) == OAMR_STATUS_OK);
    CHECK(oamr_fdcan_recovery_record_error_status(&fixture.recovery, 11u,
                                                  false, true, false,
                                                  3u, 4u) == OAMR_STATUS_OVERFLOW);
    CHECK(oamr_fdcan_recovery_record_error_status(&fixture.recovery, 12u,
                                                  false, false, true,
                                                  5u, 6u) == OAMR_STATUS_OVERFLOW);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.warning_entries == 1u);
    CHECK(snapshot.passive_entries == 1u);
    CHECK(snapshot.bus_off_events == 1u);
    CHECK(snapshot.event_overflows == 2u);
    CHECK(snapshot.transport_state == OAMR_FDCAN_TRANSPORT_BUS_OFF_WAIT);
    CHECK(oamr_bounded_queue_count(&fixture.recovery.events) == 1u);
    expect_event(&fixture.recovery, OAMR_FDCAN_RECOVERY_EVENT_WARNING,
                 10u, 0u, OAMR_STATUS_INVALID, 1u, 2u);
    memset(&untouched, 0x3c, sizeof(untouched));
    event = untouched;
    CHECK(oamr_fdcan_recovery_pop_event(&fixture.recovery, &event) ==
          OAMR_STATUS_EMPTY);
    CHECK(memcmp(&event, &untouched, sizeof(event)) == 0);

    CHECK(oamr_fdcan_recovery_record_error_status(&fixture.recovery, 13u,
                                                  true, false, false,
                                                  7u, 8u) == OAMR_STATUS_OK);
    expect_event(&fixture.recovery, OAMR_FDCAN_RECOVERY_EVENT_WARNING,
                 13u, 0u, OAMR_STATUS_INVALID, 7u, 8u);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.warning_entries == 2u);
    CHECK(snapshot.event_overflows == 2u);

    expect_poll(&fixture.recovery, UINT64_C(100012), true, 1u);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery,
                                               UINT64_C(100013),
                                               OAMR_STATUS_OK) ==
          OAMR_STATUS_OK);
    expect_event(&fixture.recovery, OAMR_FDCAN_RECOVERY_EVENT_RECOVERED,
                 UINT64_C(100013), 1u, OAMR_STATUS_OK, 7u, 8u);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.recovery_successes == 1u);
    CHECK(snapshot.transport_state == OAMR_FDCAN_TRANSPORT_ONLINE);
}

static void result_event_overflow_does_not_undo_completion(void)
{
    recovery_fixture_t fixture;
    oamr_fdcan_recovery_snapshot_t snapshot;

    memset(&fixture, 0, sizeof(fixture));
    CHECK(oamr_fdcan_recovery_init(&fixture.recovery, fixture.events,
                                   sizeof(fixture.events), 1u) ==
          OAMR_STATUS_OK);
    expect_bus_off(&fixture.recovery, 0u);
    expect_poll(&fixture.recovery, UINT64_C(100000), true, 1u);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery,
                                               UINT64_C(100001),
                                               OAMR_STATUS_TIMEOUT) ==
          OAMR_STATUS_OVERFLOW);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.recovery_failures == 1u);
    CHECK(snapshot.event_overflows == 1u);
    CHECK(snapshot.transport_state == OAMR_FDCAN_TRANSPORT_BUS_OFF_WAIT);
    expect_event(&fixture.recovery, OAMR_FDCAN_RECOVERY_EVENT_BUS_OFF,
                 0u, 0u, OAMR_STATUS_INVALID, 0u, 0u);
    expect_poll(&fixture.recovery, UINT64_C(600000), false, 0u);
    expect_poll(&fixture.recovery, UINT64_C(600001), true, 2u);
}

static void cumulative_counters_saturate_at_uint64_max(void)
{
    recovery_fixture_t fixture;
    oamr_fdcan_recovery_snapshot_t snapshot;

    memset(&fixture, 0, sizeof(fixture));
    CHECK(oamr_fdcan_recovery_init(&fixture.recovery, fixture.events,
                                   sizeof(fixture.events), 1u) ==
          OAMR_STATUS_OK);
    CHECK(oamr_fdcan_recovery_record_error_status(&fixture.recovery, 0u,
                                                  true, false, false,
                                                  0u, 0u) == OAMR_STATUS_OK);
    fixture.recovery.warning_entries = UINT64_MAX;
    fixture.recovery.passive_entries = UINT64_MAX;
    fixture.recovery.bus_off_events = UINT64_MAX;
    fixture.recovery.tx_failures = UINT64_MAX;
    fixture.recovery.rx_overflow_events = UINT64_MAX;
    fixture.recovery.tx_rejected_while_offline = UINT64_MAX;
    fixture.recovery.events.overflow_count = UINT64_MAX;

    CHECK(oamr_fdcan_recovery_record_error_status(&fixture.recovery, 1u,
                                                  true, true, true,
                                                  0u, 0u) == OAMR_STATUS_OVERFLOW);
    CHECK(oamr_fdcan_recovery_record_tx_failure(&fixture.recovery, 1u) ==
          OAMR_STATUS_OK);
    CHECK(oamr_fdcan_recovery_record_rx_overflow(&fixture.recovery, 1u) ==
          OAMR_STATUS_OK);
    CHECK(oamr_fdcan_recovery_begin_normal_tx(&fixture.recovery) ==
          OAMR_STATUS_ERROR);
    CHECK(fixture.recovery.warning_entries == UINT64_MAX);
    CHECK(fixture.recovery.passive_entries == UINT64_MAX);
    CHECK(fixture.recovery.bus_off_events == UINT64_MAX);
    CHECK(fixture.recovery.tx_failures == UINT64_MAX);
    CHECK(fixture.recovery.rx_overflow_events == UINT64_MAX);
    CHECK(fixture.recovery.tx_rejected_while_offline == UINT64_MAX);
    CHECK(fixture.recovery.events.overflow_count == UINT64_MAX);

    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.warning_entries == UINT64_MAX);
    CHECK(snapshot.passive_entries == UINT64_MAX);
    CHECK(snapshot.bus_off_events == UINT64_MAX);
    CHECK(snapshot.tx_failures == UINT64_MAX);
    CHECK(snapshot.rx_overflow_events == UINT64_MAX);
    CHECK(snapshot.tx_rejected_while_offline == UINT64_MAX);
    CHECK(snapshot.event_overflows == UINT64_MAX);

    fixture.recovery.recovery_attempts = UINT64_MAX;
    fixture.recovery.attempts_this_bus_off = UINT64_MAX;
    expect_poll(&fixture.recovery, UINT64_C(100001), true, UINT64_MAX);
    CHECK(fixture.recovery.recovery_attempts == UINT64_MAX);
    CHECK(fixture.recovery.attempts_this_bus_off == UINT64_MAX);
    fixture.recovery.recovery_failures = UINT64_MAX;
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery,
                                               UINT64_C(100001),
                                               OAMR_STATUS_ERROR) ==
          OAMR_STATUS_OVERFLOW);
    CHECK(fixture.recovery.recovery_failures == UINT64_MAX);

    fixture.recovery.recovery_successes = UINT64_MAX;
    expect_poll(&fixture.recovery, UINT64_C(1100001), true, UINT64_MAX);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery,
                                               UINT64_C(1100001),
                                               OAMR_STATUS_OK) ==
          OAMR_STATUS_OVERFLOW);
    CHECK(fixture.recovery.recovery_successes == UINT64_MAX);
}

static void transport_only_sentinel_survives_all_recovery_transitions(void)
{
    recovery_fixture_t fixture;
    transport_only_sentinel_t sentinel = sentinel_initial;
    oamr_fdcan_recovery_snapshot_t snapshot;

    fixture_init(&fixture);
    CHECK(oamr_fdcan_recovery_record_error_status(&fixture.recovery, 0u,
                                                  true, true, false,
                                                  96u, 97u) == OAMR_STATUS_OK);
    CHECK(sentinel_is_unchanged(&sentinel));
    CHECK(oamr_fdcan_recovery_begin_normal_tx(&fixture.recovery) ==
          OAMR_STATUS_OK);
    CHECK(sentinel_is_unchanged(&sentinel));

    CHECK(oamr_fdcan_recovery_record_error_status(&fixture.recovery, 1u,
                                                  true, true, true,
                                                  128u, 128u) ==
          OAMR_STATUS_OK);
    CHECK(oamr_fdcan_recovery_begin_normal_tx(&fixture.recovery) ==
          OAMR_STATUS_ERROR);
    CHECK(sentinel_is_unchanged(&sentinel));
    expect_poll(&fixture.recovery, UINT64_C(100001), true, 1u);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery,
                                               UINT64_C(100002),
                                               OAMR_STATUS_ERROR) ==
          OAMR_STATUS_OK);
    CHECK(sentinel_is_unchanged(&sentinel));
    expect_poll(&fixture.recovery, UINT64_C(600002), true, 2u);
    CHECK(oamr_fdcan_recovery_complete_attempt(&fixture.recovery,
                                               UINT64_C(600003),
                                               OAMR_STATUS_OK) ==
          OAMR_STATUS_OK);
    expect_snapshot(&fixture.recovery, &snapshot);
    CHECK(snapshot.transport_state == OAMR_FDCAN_TRANSPORT_ONLINE);
    CHECK(oamr_fdcan_recovery_begin_normal_tx(&fixture.recovery) ==
          OAMR_STATUS_OK);
    CHECK(sentinel_is_unchanged(&sentinel));
    CHECK(!sentinel.motion_permission);
    CHECK(!sentinel.lift_permission);
    CHECK(!sentinel.drive_enable);
    CHECK(!sentinel.brake_release);
    CHECK(!sentinel.contactor_enable);
    CHECK(sentinel.safety_state_cookie == UINT32_C(0x51a7e123));
}

static void deliberately_broken_fake_clock_probe(void)
{
    const uint64_t expected_now = UINT64_C(1000);
    const uint64_t broken_fake_now = UINT64_C(999);

    CHECK(broken_fake_now == expected_now);
}

static void harness_can_detect_a_broken_fake_clock(void)
{
    oamr_test_context context = {0};

    context.suppress_output = 1;
    oamr_test_run(&context, deliberately_broken_fake_clock_probe,
                  "deliberately_broken_fake_clock_probe");
    CHECK(context.tests_run == 1u);
    CHECK(context.tests_failed == 1u);
    CHECK(context.checks_run == 1u);
    CHECK(context.checks_failed == 1u);
    CHECK(oamr_test_finish(&context) == 1);
    CHECK(TEST_US_PER_MS == UINT64_C(1000));
}

int main(void)
{
    RUN(initialization_rejects_invalid_arguments);
    RUN(invalid_calls_preserve_initialized_state);
    RUN(initialization_clears_previous_episode_and_events);
    RUN(fdcan2_filter_accepts_only_exact_extended_dlc8);
    RUN(fdcan2_filter_ignores_metadata_and_payload);
    RUN(warning_passive_and_bus_off_entries_are_edge_counted);
    RUN(tx_failure_and_rx_overflow_are_independent);
    RUN(warning_and_passive_do_not_gate_normal_tx);
    RUN(first_deadline_is_exact_and_pending_is_not_duplicated);
    RUN(late_poll_does_not_create_a_catch_up_burst);
    RUN(failed_attempt_schedules_use_each_exact_non_ok_result);
    RUN(failed_attempt_schedules_timeout_overflow_and_error);
    RUN(successful_completion_restores_transport_only_once);
    RUN(repeated_reports_do_not_reset_wait_or_pending_attempt);
    RUN(new_bus_off_after_success_restarts_first_delay);
    RUN(equal_time_and_regressions_preserve_each_operation);
    RUN(elapsed_deadlines_above_32_bits_do_not_wrap);
    RUN(uint64_max_boundaries_have_no_wrapped_early_attempt);
    RUN(event_queue_overflow_preserves_state_and_fifo_order);
    RUN(result_event_overflow_does_not_undo_completion);
    RUN(cumulative_counters_saturate_at_uint64_max);
    RUN(transport_only_sentinel_survives_all_recovery_transitions);
    RUN(harness_can_detect_a_broken_fake_clock);

    return TEST_END();
}
