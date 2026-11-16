/* SPDX-License-Identifier: MIT */
#include "oamr/can_health.h"

#include <string.h>

void oamr_can_health_default_config(oamr_can_health_config_t *cfg)
{
    cfg->restart_delay_us = 100000u;   /* TODO(Evans) */
    cfg->max_events = 3u;              /* TODO(Evans) */
    cfg->window_us = 10000000u;        /* TODO(Evans) */
}

void oamr_can_health_init(oamr_can_health_t *h, const oamr_can_health_config_t *cfg)
{
    memset(h, 0, sizeof(*h));
    h->cfg = *cfg;
    if (h->cfg.max_events > OAMR_CAN_HEALTH_MAX_EVENTS) {
        h->cfg.max_events = OAMR_CAN_HEALTH_MAX_EVENTS;
    }
    h->state = OAMR_CAN_OK;
}

static uint32_t events_in_window(const oamr_can_health_t *h, uint64_t now_us)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < h->event_count; i++) {
        if (now_us - h->event_us[i] < h->cfg.window_us) {
            n++;
        }
    }
    return n;
}

static void record_event(oamr_can_health_t *h, uint64_t now_us)
{
    h->event_us[h->event_head] = now_us;
    h->event_head = (h->event_head + 1u) % OAMR_CAN_HEALTH_MAX_EVENTS;
    if (h->event_count < OAMR_CAN_HEALTH_MAX_EVENTS) {
        h->event_count++;
    }
    h->bus_off_total++;
}

oamr_can_action_t oamr_can_health_update(oamr_can_health_t *h, uint64_t now_us, bool bus_off)
{
    switch (h->state) {
    case OAMR_CAN_OK:
        if (bus_off) {
            record_event(h, now_us);
            h->bus_off_since_us = now_us;
            h->state = (events_in_window(h, now_us) > h->cfg.max_events) ? OAMR_CAN_FAILED
                                                                          : OAMR_CAN_BUS_OFF_WAIT;
        }
        return OAMR_CAN_ACTION_NONE;
    case OAMR_CAN_BUS_OFF_WAIT:
        if (now_us - h->bus_off_since_us >= h->cfg.restart_delay_us) {
            h->state = OAMR_CAN_RESTARTING;
            h->restarts_total++;
            return OAMR_CAN_ACTION_RESTART;
        }
        return OAMR_CAN_ACTION_NONE;
    case OAMR_CAN_RESTARTING:
        if (!bus_off) {
            h->state = OAMR_CAN_OK;
        } else if (now_us - h->bus_off_since_us >= h->cfg.restart_delay_us) {
            /* Restart did not take: count it as a further event. */
            record_event(h, now_us);
            h->bus_off_since_us = now_us;
            h->state = (events_in_window(h, now_us) > h->cfg.max_events) ? OAMR_CAN_FAILED
                                                                          : OAMR_CAN_BUS_OFF_WAIT;
        }
        return OAMR_CAN_ACTION_NONE;
    case OAMR_CAN_FAILED:
    default:
        return OAMR_CAN_ACTION_NONE;
    }
}

bool oamr_can_health_operator_reset(oamr_can_health_t *h)
{
    if (h->state != OAMR_CAN_FAILED) {
        return false;
    }
    h->event_count = 0;
    h->event_head = 0;
    h->state = OAMR_CAN_BUS_OFF_WAIT;
    h->bus_off_since_us = 0;
    return true;
}
