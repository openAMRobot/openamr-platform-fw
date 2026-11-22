/* SPDX-License-Identifier: MIT */
/*
 * Bus-off supervision for one CAN controller.
 *
 * The board port reports the controller's bus-off flag every control tick.
 * The core decides when the port may restart the controller: after a hold-off
 * delay, and at most `max_events` times within `window_us`. Beyond that the
 * channel latches FAILED and stays off until an explicit operator reset, so a
 * flapping bus never keeps restarting silently. Every event is counted for the
 * status message.
 */
#ifndef OAMR_CAN_HEALTH_H
#define OAMR_CAN_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OAMR_CAN_HEALTH_MAX_EVENTS 8u

typedef struct {
    uint64_t restart_delay_us;   /* TODO(Evans): hold-off before restart */
    uint32_t max_events;         /* TODO(Evans): bus-off events tolerated per window (<= 8) */
    uint64_t window_us;          /* TODO(Evans): window length */
} oamr_can_health_config_t;

typedef enum {
    OAMR_CAN_OK = 0,
    OAMR_CAN_BUS_OFF_WAIT,       /* bus-off seen, waiting for the hold-off */
    OAMR_CAN_RESTARTING,         /* restart requested, waiting for error-active */
    OAMR_CAN_FAILED              /* latched; operator reset required */
} oamr_can_health_state_t;

typedef enum {
    OAMR_CAN_ACTION_NONE = 0,
    OAMR_CAN_ACTION_RESTART      /* port must restart the controller now */
} oamr_can_action_t;

typedef struct {
    oamr_can_health_config_t cfg;
    oamr_can_health_state_t state;
    uint64_t bus_off_since_us;
    uint64_t event_us[OAMR_CAN_HEALTH_MAX_EVENTS];
    uint32_t event_head;
    uint32_t event_count;        /* entries valid in event_us */
    uint32_t bus_off_total;      /* lifetime counter for status */
    uint32_t restarts_total;
} oamr_can_health_t;

void oamr_can_health_default_config(oamr_can_health_config_t *cfg);
void oamr_can_health_init(oamr_can_health_t *h, const oamr_can_health_config_t *cfg);
oamr_can_action_t oamr_can_health_update(oamr_can_health_t *h, uint64_t now_us, bool bus_off);
/* Operator reset of a latched channel. Returns true if the latch was cleared. */
bool oamr_can_health_operator_reset(oamr_can_health_t *h);
static inline bool oamr_can_health_usable(const oamr_can_health_t *h)
{
    return h->state == OAMR_CAN_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* OAMR_CAN_HEALTH_H */
