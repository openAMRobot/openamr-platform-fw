#include "oamr/sensor_scheduler.h"

#include <stddef.h>
#include <string.h>

static void increment(uint64_t *counter)
{
    if (*counter < UINT64_MAX) {
        ++*counter;
    }
}

static oamr_sensor_decision_t decision(oamr_sensor_step_t action,
                                       uint8_t sensor)
{
    oamr_sensor_decision_t result;

    result.action = action;
    result.sensor = sensor;
    return result;
}

static bool valid_scheduler(const oamr_sensor_scheduler_t *scheduler)
{
    if (scheduler == NULL || !scheduler->configured ||
        scheduler->config.sensor_count == 0u ||
        scheduler->config.sensor_count > OAMR_SENSOR_SCHEDULER_MAX_SENSORS ||
        scheduler->current_sensor >= scheduler->config.sensor_count ||
        scheduler->proposed_sensor >= scheduler->config.sensor_count ||
        (scheduler->outstanding && scheduler->trigger_pending)) {
        return false;
    }

    if (scheduler->has_time) {
        if (scheduler->outstanding && scheduler->trigger_us > scheduler->last_now_us) {
            return false;
        }
        if (scheduler->trigger_pending && scheduler->proposed_us > scheduler->last_now_us) {
            return false;
        }
        if (scheduler->started && !scheduler->outstanding &&
            scheduler->done_us > scheduler->last_now_us) {
            return false;
        }
    } else if (scheduler->started || scheduler->outstanding || scheduler->trigger_pending) {
        return false;
    }
    return true;
}

oamr_status_t oamr_sensor_scheduler_init(
    oamr_sensor_scheduler_t *scheduler,
    const oamr_sensor_scheduler_config_t *config)
{
    if (scheduler == NULL) {
        return OAMR_STATUS_INVALID;
    }

    (void)memset(scheduler, 0, sizeof(*scheduler));
    if (config == NULL || config->sensor_count == 0u ||
        config->sensor_count > OAMR_SENSOR_SCHEDULER_MAX_SENSORS) {
        return OAMR_STATUS_INVALID;
    }

    scheduler->config = *config;
    scheduler->configured = true;
    return OAMR_STATUS_OK;
}

oamr_sensor_decision_t oamr_sensor_scheduler_step(
    oamr_sensor_scheduler_t *scheduler,
    uint64_t now_us)
{
    if (scheduler == NULL || !scheduler->configured) {
        return decision(OAMR_SENSOR_STEP_NOT_CONFIGURED, 0u);
    }
    if (scheduler->has_time && now_us < scheduler->last_now_us) {
        increment(&scheduler->counters.time_errors);
        return decision(OAMR_SENSOR_STEP_WAIT, scheduler->current_sensor);
    }
    if (!valid_scheduler(scheduler)) {
        return decision(OAMR_SENSOR_STEP_NOT_CONFIGURED, 0u);
    }
    scheduler->last_now_us = now_us;
    scheduler->has_time = true;

    if (scheduler->outstanding) {
        uint64_t elapsed = now_us - scheduler->trigger_us;
        uint8_t sensor = scheduler->current_sensor;

        if (elapsed >= scheduler->config.reading_timeout_us) {
            scheduler->outstanding = false;
            scheduler->done_us = now_us;
            scheduler->trigger_pending = false;
            increment(&scheduler->counters.timeouts[sensor]);
            return decision(OAMR_SENSOR_STEP_TIMEOUT, sensor);
        }
        return decision(OAMR_SENSOR_STEP_WAIT, sensor);
    }

    if (scheduler->started && now_us - scheduler->done_us < scheduler->config.gap_us) {
        return decision(OAMR_SENSOR_STEP_WAIT, scheduler->current_sensor);
    }

    if (scheduler->started) {
        scheduler->proposed_sensor = (uint8_t)(scheduler->current_sensor + 1u);
        if (scheduler->proposed_sensor == scheduler->config.sensor_count) {
            scheduler->proposed_sensor = 0u;
        }
    } else {
        scheduler->proposed_sensor = 0u;
    }
    scheduler->proposed_us = now_us;
    scheduler->trigger_pending = true;
    return decision(OAMR_SENSOR_STEP_TRIGGER, scheduler->proposed_sensor);
}

oamr_status_t oamr_sensor_scheduler_triggered(
    oamr_sensor_scheduler_t *scheduler,
    uint8_t sensor,
    uint64_t now_us)
{
    if (scheduler == NULL || !scheduler->configured) {
        return OAMR_STATUS_INVALID;
    }
    if (scheduler->has_time && now_us < scheduler->last_now_us) {
        increment(&scheduler->counters.time_errors);
        return OAMR_STATUS_INVALID;
    }
    if (!valid_scheduler(scheduler)) {
        return OAMR_STATUS_INVALID;
    }
    if (!scheduler->trigger_pending || scheduler->outstanding ||
        sensor != scheduler->proposed_sensor || sensor >= scheduler->config.sensor_count ||
        now_us < scheduler->proposed_us) {
        increment(&scheduler->counters.rejected_triggers);
        return OAMR_STATUS_INVALID;
    }

    scheduler->current_sensor = sensor;
    scheduler->last_now_us = now_us;
    scheduler->has_time = true;
    scheduler->trigger_us = now_us;
    scheduler->outstanding = true;
    scheduler->trigger_pending = false;
    scheduler->started = true;
    return OAMR_STATUS_OK;
}

oamr_status_t oamr_sensor_scheduler_reading(
    oamr_sensor_scheduler_t *scheduler,
    uint8_t sensor,
    uint64_t now_us)
{
    uint64_t elapsed;

    if (scheduler == NULL || !scheduler->configured) {
        return OAMR_STATUS_INVALID;
    }
    if (scheduler->has_time && now_us < scheduler->last_now_us) {
        increment(&scheduler->counters.time_errors);
        return OAMR_STATUS_INVALID;
    }
    if (!valid_scheduler(scheduler)) {
        return OAMR_STATUS_INVALID;
    }
    if (!scheduler->outstanding || sensor != scheduler->current_sensor ||
        now_us < scheduler->trigger_us) {
        increment(&scheduler->counters.late_frames);
        return OAMR_STATUS_INVALID;
    }

    elapsed = now_us - scheduler->trigger_us;
    if (elapsed >= scheduler->config.reading_timeout_us) {
        increment(&scheduler->counters.late_frames);
        return OAMR_STATUS_TIMEOUT;
    }

    scheduler->outstanding = false;
    scheduler->trigger_pending = false;
    scheduler->done_us = now_us;
    scheduler->last_now_us = now_us;
    scheduler->has_time = true;
    return OAMR_STATUS_OK;
}
