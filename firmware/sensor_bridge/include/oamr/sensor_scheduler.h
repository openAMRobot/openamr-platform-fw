#ifndef OAMR_SENSOR_SCHEDULER_H
#define OAMR_SENSOR_SCHEDULER_H

#include "oamr/status.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pure decision helper for sequential triggering of one through four sensors.
 * It never drives a pin and imposes no timing defaults or actuation policy.
 *
 * step() proposes a trigger; triggered() accepts only that proposed sensor,
 * at the same or a later timestamp. Exactly one reading can then be accepted,
 * and only for the outstanding sensor before its timeout boundary. At elapsed
 * == reading_timeout_us, step() reports TIMEOUT once and a reading is late.
 * Completion or timeout starts the caller-configured inter-sensor gap.
 */
#define OAMR_SENSOR_SCHEDULER_MAX_SENSORS 4u

typedef struct {
    uint8_t sensor_count;
    uint64_t reading_timeout_us;
    uint64_t gap_us;
} oamr_sensor_scheduler_config_t;

typedef enum {
    OAMR_SENSOR_STEP_NOT_CONFIGURED = 0,
    OAMR_SENSOR_STEP_WAIT = 1,
    OAMR_SENSOR_STEP_TRIGGER = 2,
    OAMR_SENSOR_STEP_TIMEOUT = 3
} oamr_sensor_step_t;

typedef struct {
    oamr_sensor_step_t action;
    uint8_t sensor;
} oamr_sensor_decision_t;

typedef struct {
    uint64_t timeouts[OAMR_SENSOR_SCHEDULER_MAX_SENSORS];
    uint64_t late_frames;
    uint64_t rejected_triggers;
    uint64_t time_errors;
} oamr_sensor_counters_t;

typedef struct {
    oamr_sensor_scheduler_config_t config;
    bool configured;
    bool started;
    bool trigger_pending;
    bool outstanding;
    uint8_t proposed_sensor;
    uint8_t current_sensor;
    uint64_t proposed_us;
    uint64_t trigger_us;
    uint64_t done_us;
    uint64_t last_now_us;
    bool has_time;
    oamr_sensor_counters_t counters;
} oamr_sensor_scheduler_t;

/*
 * A zero reading_timeout_us is an explicit immediate deadline; a zero gap_us
 * is an explicit zero gap. Invalid arguments leave a non-NULL scheduler
 * cleared and unconfigured.
 */
oamr_status_t oamr_sensor_scheduler_init(
    oamr_sensor_scheduler_t *scheduler,
    const oamr_sensor_scheduler_config_t *config);

/*
 * Return the action for now_us. A decreasing timestamp is rejected as WAIT,
 * increments time_errors, and leaves acquisition state unchanged. A NULL or
 * unconfigured scheduler returns NOT_CONFIGURED. TRIGGER may be returned
 * repeatedly until confirmed.
 */
oamr_sensor_decision_t oamr_sensor_scheduler_step(
    oamr_sensor_scheduler_t *scheduler,
    uint64_t now_us);

/*
 * Confirm the actual pin trigger. Only the sensor most recently proposed by
 * step() is valid, and only while no reading is outstanding. Rejection returns
 * INVALID and does not create an outstanding reading.
 */
oamr_status_t oamr_sensor_scheduler_triggered(
    oamr_sensor_scheduler_t *scheduler,
    uint8_t sensor,
    uint64_t now_us);

/*
 * Report a parsed reading. OK completes the one outstanding acquisition. A
 * wrong sensor or duplicate/unsolicited reading is INVALID. Time before the
 * trigger is INVALID; time before the last accepted reading is also INVALID.
 * A reading at or after its deadline is TIMEOUT, remains late/rejected, and is
 * still followed by one TIMEOUT decision from step(). All counters saturate.
 */
oamr_status_t oamr_sensor_scheduler_reading(
    oamr_sensor_scheduler_t *scheduler,
    uint8_t sensor,
    uint64_t now_us);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_SENSOR_SCHEDULER_H */
