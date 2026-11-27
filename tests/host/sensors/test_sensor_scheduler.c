/* SPDX-License-Identifier: MIT */
#include "test_harness.h"

#include "oamr/sensor_scheduler.h"

#include <stddef.h>
#include <stdint.h>

static oamr_sensor_scheduler_config_t valid_config(void)
{
    oamr_sensor_scheduler_config_t config;

    config.sensor_count = 2u;
    config.reading_timeout_us = UINT64_C(100);
    config.gap_us = UINT64_C(20);
    return config;
}

static void expect_step(oamr_sensor_scheduler_t *scheduler, uint64_t now_us,
                        oamr_sensor_step_t action, uint8_t sensor)
{
    oamr_sensor_decision_t decision = oamr_sensor_scheduler_step(scheduler, now_us);

    CHECK(decision.action == action);
    CHECK(decision.sensor == sensor);
}

static void invalid_configuration_and_null_calls_are_safe(void)
{
    oamr_sensor_scheduler_t scheduler;
    oamr_sensor_scheduler_config_t config = valid_config();
    oamr_sensor_decision_t decision;

    CHECK(oamr_sensor_scheduler_init(NULL, &config) == OAMR_STATUS_INVALID);
    CHECK(oamr_sensor_scheduler_init(&scheduler, NULL) == OAMR_STATUS_INVALID);
    CHECK(!scheduler.configured);
    CHECK(!scheduler.started);
    expect_step(&scheduler, UINT64_C(0), OAMR_SENSOR_STEP_NOT_CONFIGURED, 0u);

    config.sensor_count = 0u;
    CHECK(oamr_sensor_scheduler_init(&scheduler, &config) == OAMR_STATUS_INVALID);
    CHECK(!scheduler.configured);
    config.sensor_count = OAMR_SENSOR_SCHEDULER_MAX_SENSORS + 1u;
    CHECK(oamr_sensor_scheduler_init(&scheduler, &config) == OAMR_STATUS_INVALID);
    CHECK(!scheduler.configured);

    config = valid_config();
    config.reading_timeout_us = UINT64_C(0);
    config.gap_us = UINT64_C(0);
    CHECK(oamr_sensor_scheduler_init(&scheduler, &config) == OAMR_STATUS_OK);
    CHECK(scheduler.configured);
    expect_step(&scheduler, UINT64_C(10), OAMR_SENSOR_STEP_TRIGGER, 0u);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 0u, UINT64_C(10)) == OAMR_STATUS_OK);
    expect_step(&scheduler, UINT64_C(10), OAMR_SENSOR_STEP_TIMEOUT, 0u);
    expect_step(&scheduler, UINT64_C(10), OAMR_SENSOR_STEP_TRIGGER, 1u);

    decision = oamr_sensor_scheduler_step(NULL, UINT64_C(0));
    CHECK(decision.action == OAMR_SENSOR_STEP_NOT_CONFIGURED);
    CHECK(decision.sensor == 0u);
    CHECK(oamr_sensor_scheduler_triggered(NULL, 0u, UINT64_C(0)) == OAMR_STATUS_INVALID);
    CHECK(oamr_sensor_scheduler_reading(NULL, 0u, UINT64_C(0)) == OAMR_STATUS_INVALID);
}

static void only_one_outstanding_sensor_and_explicit_trigger_confirm(void)
{
    oamr_sensor_scheduler_t scheduler;
    oamr_sensor_scheduler_config_t config = valid_config();

    CHECK(oamr_sensor_scheduler_init(&scheduler, &config) == OAMR_STATUS_OK);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 0u, UINT64_C(0)) == OAMR_STATUS_INVALID);
    CHECK(!scheduler.outstanding);
    CHECK(scheduler.counters.rejected_triggers == UINT64_C(1));

    expect_step(&scheduler, UINT64_C(100), OAMR_SENSOR_STEP_TRIGGER, 0u);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 1u, UINT64_C(100)) == OAMR_STATUS_INVALID);
    CHECK(!scheduler.outstanding);
    CHECK(scheduler.counters.rejected_triggers == UINT64_C(2));
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 0u, UINT64_C(100)) == OAMR_STATUS_OK);
    CHECK(scheduler.outstanding);
    CHECK(scheduler.current_sensor == 0u);

    expect_step(&scheduler, UINT64_C(101), OAMR_SENSOR_STEP_WAIT, 0u);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 1u, UINT64_C(101)) == OAMR_STATUS_INVALID);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 0u, UINT64_C(101)) == OAMR_STATUS_INVALID);
    CHECK(scheduler.current_sensor == 0u);
    CHECK(scheduler.counters.rejected_triggers == UINT64_C(4));

    CHECK(oamr_sensor_scheduler_reading(&scheduler, 0u, UINT64_C(101)) == OAMR_STATUS_OK);
    CHECK(!scheduler.outstanding);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 0u, UINT64_C(101)) == OAMR_STATUS_INVALID);
    expect_step(&scheduler, UINT64_C(120), OAMR_SENSOR_STEP_WAIT, 0u);
    expect_step(&scheduler, UINT64_C(121), OAMR_SENSOR_STEP_TRIGGER, 1u);
}

static void unconfirmed_trigger_may_be_proposed_again(void)
{
    oamr_sensor_scheduler_t scheduler;
    oamr_sensor_scheduler_config_t config = valid_config();

    CHECK(oamr_sensor_scheduler_init(&scheduler, &config) == OAMR_STATUS_OK);
    expect_step(&scheduler, UINT64_C(5), OAMR_SENSOR_STEP_TRIGGER, 0u);
    expect_step(&scheduler, UINT64_C(6), OAMR_SENSOR_STEP_TRIGGER, 0u);
    CHECK(scheduler.trigger_pending);
    CHECK(!scheduler.outstanding);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 0u, UINT64_C(6)) == OAMR_STATUS_OK);
    CHECK(scheduler.trigger_us == UINT64_C(6));
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 0u, UINT64_C(105)) == OAMR_STATUS_OK);
}

static void wrong_sensor_duplicate_and_unsolicited_readings_are_rejected(void)
{
    oamr_sensor_scheduler_t scheduler;
    oamr_sensor_scheduler_config_t config = valid_config();

    CHECK(oamr_sensor_scheduler_init(&scheduler, &config) == OAMR_STATUS_OK);
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 0u, UINT64_C(1)) == OAMR_STATUS_INVALID);
    expect_step(&scheduler, UINT64_C(10), OAMR_SENSOR_STEP_TRIGGER, 0u);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 0u, UINT64_C(10)) == OAMR_STATUS_OK);
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 1u, UINT64_C(11)) == OAMR_STATUS_INVALID);
    CHECK(scheduler.outstanding);
    CHECK(scheduler.current_sensor == 0u);
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 0u, UINT64_C(12)) == OAMR_STATUS_OK);
    CHECK(!scheduler.outstanding);
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 0u, UINT64_C(12)) == OAMR_STATUS_INVALID);
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 1u, UINT64_C(12)) == OAMR_STATUS_INVALID);
    expect_step(&scheduler, UINT64_C(32), OAMR_SENSOR_STEP_TRIGGER, 1u);
}

static void deadline_is_inclusive_and_timeout_is_reported_once(void)
{
    oamr_sensor_scheduler_t scheduler;
    oamr_sensor_scheduler_config_t config = valid_config();

    CHECK(oamr_sensor_scheduler_init(&scheduler, &config) == OAMR_STATUS_OK);
    expect_step(&scheduler, UINT64_C(1000), OAMR_SENSOR_STEP_TRIGGER, 0u);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 0u, UINT64_C(1000)) == OAMR_STATUS_OK);
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 0u, UINT64_C(1099)) == OAMR_STATUS_OK);
    expect_step(&scheduler, UINT64_C(1120), OAMR_SENSOR_STEP_TRIGGER, 1u);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 1u, UINT64_C(1120)) == OAMR_STATUS_OK);
    expect_step(&scheduler, UINT64_C(1219), OAMR_SENSOR_STEP_WAIT, 1u);
    CHECK(scheduler.counters.timeouts[1] == UINT64_C(0));

    expect_step(&scheduler, UINT64_C(1220), OAMR_SENSOR_STEP_TIMEOUT, 1u);
    CHECK(!scheduler.outstanding);
    CHECK(scheduler.counters.timeouts[1] == UINT64_C(1));
    expect_step(&scheduler, UINT64_C(1239), OAMR_SENSOR_STEP_WAIT, 1u);
    CHECK(scheduler.counters.timeouts[1] == UINT64_C(1));
    expect_step(&scheduler, UINT64_C(1240), OAMR_SENSOR_STEP_TRIGGER, 0u);
    CHECK(scheduler.counters.timeouts[1] == UINT64_C(1));
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 1u, UINT64_C(1240)) == OAMR_STATUS_INVALID);
    CHECK(scheduler.counters.late_frames == UINT64_C(1));
}

static void late_reading_closes_once_and_step_still_reports_timeout(void)
{
    oamr_sensor_scheduler_t scheduler;
    oamr_sensor_scheduler_config_t config = valid_config();

    CHECK(oamr_sensor_scheduler_init(&scheduler, &config) == OAMR_STATUS_OK);
    expect_step(&scheduler, UINT64_C(0), OAMR_SENSOR_STEP_TRIGGER, 0u);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 0u, UINT64_C(0)) == OAMR_STATUS_OK);
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 0u, UINT64_C(100)) == OAMR_STATUS_TIMEOUT);
    CHECK(scheduler.outstanding);
    CHECK(scheduler.counters.late_frames == UINT64_C(1));
    CHECK(scheduler.counters.timeouts[0] == UINT64_C(0));
    expect_step(&scheduler, UINT64_C(100), OAMR_SENSOR_STEP_TIMEOUT, 0u);
    CHECK(!scheduler.outstanding);
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 0u, UINT64_C(101)) == OAMR_STATUS_INVALID);
    expect_step(&scheduler, UINT64_C(119), OAMR_SENSOR_STEP_WAIT, 0u);
    CHECK(scheduler.counters.timeouts[0] == UINT64_C(1));
    expect_step(&scheduler, UINT64_C(120), OAMR_SENSOR_STEP_TRIGGER, 1u);
    CHECK(scheduler.counters.timeouts[0] == UINT64_C(1));
    CHECK(scheduler.counters.late_frames == UINT64_C(2));
}

static void reading_after_step_timeout_is_rejected_without_second_timeout(void)
{
    oamr_sensor_scheduler_t scheduler;
    oamr_sensor_scheduler_config_t config = valid_config();

    CHECK(oamr_sensor_scheduler_init(&scheduler, &config) == OAMR_STATUS_OK);
    expect_step(&scheduler, UINT64_C(10), OAMR_SENSOR_STEP_TRIGGER, 0u);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 0u, UINT64_C(10)) == OAMR_STATUS_OK);
    expect_step(&scheduler, UINT64_C(110), OAMR_SENSOR_STEP_TIMEOUT, 0u);
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 0u, UINT64_C(111)) == OAMR_STATUS_INVALID);
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 0u, UINT64_C(112)) == OAMR_STATUS_INVALID);
    CHECK(scheduler.counters.timeouts[0] == UINT64_C(1));
    CHECK(scheduler.counters.late_frames == UINT64_C(2));
}

static void regressing_time_does_not_change_acquisition_state(void)
{
    oamr_sensor_scheduler_t scheduler;
    oamr_sensor_scheduler_config_t config = valid_config();

    CHECK(oamr_sensor_scheduler_init(&scheduler, &config) == OAMR_STATUS_OK);
    expect_step(&scheduler, UINT64_C(100), OAMR_SENSOR_STEP_TRIGGER, 0u);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 0u, UINT64_C(100)) == OAMR_STATUS_OK);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 0u, UINT64_C(99)) == OAMR_STATUS_INVALID);
    CHECK(scheduler.trigger_us == UINT64_C(100));
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 0u, UINT64_C(99)) == OAMR_STATUS_INVALID);
    CHECK(scheduler.outstanding);
    CHECK(scheduler.counters.time_errors == UINT64_C(2));

    expect_step(&scheduler, UINT64_C(99), OAMR_SENSOR_STEP_WAIT, 0u);
    CHECK(scheduler.outstanding);
    CHECK(scheduler.trigger_us == UINT64_C(100));
    CHECK(scheduler.counters.time_errors == UINT64_C(3));
    CHECK(scheduler.counters.timeouts[0] == UINT64_C(0));
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 0u, UINT64_C(199)) == OAMR_STATUS_OK);
    CHECK(!scheduler.outstanding);
    expect_step(&scheduler, UINT64_C(219), OAMR_SENSOR_STEP_TRIGGER, 1u);
}

static void gaps_are_inclusive_and_zero_gap_is_explicit(void)
{
    oamr_sensor_scheduler_t scheduler;
    oamr_sensor_scheduler_config_t config = valid_config();

    config.gap_us = UINT64_C(0);
    CHECK(oamr_sensor_scheduler_init(&scheduler, &config) == OAMR_STATUS_OK);
    expect_step(&scheduler, UINT64_C(50), OAMR_SENSOR_STEP_TRIGGER, 0u);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 0u, UINT64_C(50)) == OAMR_STATUS_OK);
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 0u, UINT64_C(51)) == OAMR_STATUS_OK);
    expect_step(&scheduler, UINT64_C(51), OAMR_SENSOR_STEP_TRIGGER, 1u);

    config.gap_us = UINT64_C(10);
    CHECK(oamr_sensor_scheduler_init(&scheduler, &config) == OAMR_STATUS_OK);
    expect_step(&scheduler, UINT64_C(0), OAMR_SENSOR_STEP_TRIGGER, 0u);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 0u, UINT64_C(0)) == OAMR_STATUS_OK);
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 0u, UINT64_C(5)) == OAMR_STATUS_OK);
    expect_step(&scheduler, UINT64_C(14), OAMR_SENSOR_STEP_WAIT, 0u);
    expect_step(&scheduler, UINT64_C(15), OAMR_SENSOR_STEP_TRIGGER, 1u);
}

static void uint64_timeline_beyond_32_bits_has_no_wrap_or_truncation(void)
{
    oamr_sensor_scheduler_t scheduler;
    oamr_sensor_scheduler_config_t config = valid_config();

    CHECK(oamr_sensor_scheduler_init(&scheduler, &config) == OAMR_STATUS_OK);
    expect_step(&scheduler, UINT64_C(0x00000000fffffffe), OAMR_SENSOR_STEP_TRIGGER, 0u);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 0u,
        UINT64_C(0x00000000fffffffe)) == OAMR_STATUS_OK);
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 0u,
        UINT64_C(0x0000000100000000)) == OAMR_STATUS_OK);
    CHECK(scheduler.done_us == UINT64_C(0x0000000100000000));
    expect_step(&scheduler, UINT64_C(0x0000000100000014), OAMR_SENSOR_STEP_TRIGGER, 1u);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 1u,
        UINT64_C(0x0000000100000014)) == OAMR_STATUS_OK);
    expect_step(&scheduler, UINT64_C(0x0000000100000077), OAMR_SENSOR_STEP_WAIT, 1u);
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 1u,
        UINT64_C(0x0000000100000077)) == OAMR_STATUS_OK);
    expect_step(&scheduler, UINT64_C(0x0000000200000000), OAMR_SENSOR_STEP_TRIGGER, 0u);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 0u,
        UINT64_C(0x0000000200000000)) == OAMR_STATUS_OK);
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 0u,
        UINT64_C(0x0000000200000001)) == OAMR_STATUS_OK);
    CHECK(scheduler.counters.time_errors == UINT64_C(0));
}

static void extreme_time_values_do_not_overflow_deadlines(void)
{
    oamr_sensor_scheduler_t scheduler;
    oamr_sensor_scheduler_config_t config = valid_config();

    /* (UINT64_MAX - 1) - (UINT64_MAX - 102) == 101, before timeout 120. */
    config.reading_timeout_us = UINT64_C(120);
    CHECK(oamr_sensor_scheduler_init(&scheduler, &config) == OAMR_STATUS_OK);
    expect_step(&scheduler, UINT64_MAX - UINT64_C(102), OAMR_SENSOR_STEP_TRIGGER, 0u);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 0u,
        UINT64_MAX - UINT64_C(102)) == OAMR_STATUS_OK);
    expect_step(&scheduler, UINT64_MAX - UINT64_C(1), OAMR_SENSOR_STEP_WAIT, 0u);
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 0u,
        UINT64_MAX - UINT64_C(1)) == OAMR_STATUS_OK);
    CHECK(scheduler.done_us == UINT64_MAX - UINT64_C(1));
    expect_step(&scheduler, UINT64_MAX, OAMR_SENSOR_STEP_WAIT, 0u);

    /* UINT64_MAX - (UINT64_MAX - 100) == 100, exactly the deadline. */
    config.reading_timeout_us = UINT64_C(100);
    CHECK(oamr_sensor_scheduler_init(&scheduler, &config) == OAMR_STATUS_OK);
    expect_step(&scheduler, UINT64_MAX - UINT64_C(100), OAMR_SENSOR_STEP_TRIGGER, 0u);
    CHECK(oamr_sensor_scheduler_triggered(&scheduler, 0u,
        UINT64_MAX - UINT64_C(100)) == OAMR_STATUS_OK);
    CHECK(oamr_sensor_scheduler_reading(&scheduler, 0u, UINT64_MAX) == OAMR_STATUS_TIMEOUT);
    expect_step(&scheduler, UINT64_MAX, OAMR_SENSOR_STEP_TIMEOUT, 0u);
    expect_step(&scheduler, UINT64_MAX, OAMR_SENSOR_STEP_WAIT, 0u);
    CHECK(scheduler.counters.timeouts[0] == UINT64_C(1));
}

int main(void)
{
    RUN(invalid_configuration_and_null_calls_are_safe);
    RUN(only_one_outstanding_sensor_and_explicit_trigger_confirm);
    RUN(unconfirmed_trigger_may_be_proposed_again);
    RUN(wrong_sensor_duplicate_and_unsolicited_readings_are_rejected);
    RUN(deadline_is_inclusive_and_timeout_is_reported_once);
    RUN(late_reading_closes_once_and_step_still_reports_timeout);
    RUN(reading_after_step_timeout_is_rejected_without_second_timeout);
    RUN(regressing_time_does_not_change_acquisition_state);
    RUN(gaps_are_inclusive_and_zero_gap_is_explicit);
    RUN(uint64_timeline_beyond_32_bits_has_no_wrap_or_truncation);
    RUN(extreme_time_values_do_not_overflow_deadlines);
    return TEST_END();
}
