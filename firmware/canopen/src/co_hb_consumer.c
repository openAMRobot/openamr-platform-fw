/* SPDX-License-Identifier: MIT */
#include "oamr/co_hb_consumer.h"

#include <string.h>

#include "oamr/co_cobid.h"

oamr_co_hb_class_t oamr_co_hb_init(oamr_co_hb_consumer_t *c, const oamr_co_hb_config_t *cfg,
                                   uint64_t now_us)
{
    memset(c, 0, sizeof(*c));
    if (cfg == 0 || cfg->timeout_us == 0u || !oamr_co_node_id_valid(cfg->node_id)) {
        c->last_class = OAMR_CO_HB_NOT_CONFIGURED;
        return c->last_class;
    }
    c->cfg = *cfg;
    c->configured = true;
    c->start_us = now_us;
    c->last_class = OAMR_CO_HB_WAITING;
    return c->last_class;
}

bool oamr_co_hb_on_frame(oamr_co_hb_consumer_t *c, const oamr_co_frame_t *f, uint64_t now_us)
{
    if (!c->configured) {
        return false;
    }
    oamr_co_nmt_state_t st;
    const oamr_co_dec_t d = oamr_co_decode_heartbeat(f, c->cfg.node_id, &st);
    if (d == OAMR_CO_DEC_NOT_FOR_US) {
        return false;
    }
    if (d == OAMR_CO_DEC_MALFORMED) {
        c->malformed++;
        return true; /* never refreshes last_seen_us */
    }
    if ((c->seen && now_us < c->last_seen_us) || now_us < c->start_us) {
        c->clock_errors++;
        return true; /* a frame stamped in the past is not fresh evidence */
    }
    if (c->seen && st != c->last_state) {
        c->state_changes++;
    }
    if (st == OAMR_CO_NMT_BOOTUP) {
        c->bootups++;
    }
    c->heartbeats++;
    c->seen = true;
    c->last_seen_us = now_us;
    c->last_state = st;
    return true;
}

oamr_co_hb_class_t oamr_co_hb_classify(oamr_co_hb_consumer_t *c, uint64_t now_us)
{
    if (!c->configured) {
        return OAMR_CO_HB_NOT_CONFIGURED;
    }
    oamr_co_hb_class_t k;
    const uint64_t ref = c->seen ? c->last_seen_us : c->start_us;
    if (now_us < ref) {
        c->clock_errors++;
        k = OAMR_CO_HB_LOST; /* time went backwards: not evidence of a live node */
    } else if (now_us - ref >= c->cfg.timeout_us) {
        k = OAMR_CO_HB_LOST;
    } else {
        k = c->seen ? OAMR_CO_HB_ALIVE : OAMR_CO_HB_WAITING;
    }
    if (k == OAMR_CO_HB_LOST && c->last_class != OAMR_CO_HB_LOST) {
        c->lost_events++;
    }
    c->last_class = k;
    return k;
}

const char *oamr_co_hb_class_name(oamr_co_hb_class_t k)
{
    switch (k) {
    case OAMR_CO_HB_NOT_CONFIGURED: return "NOT_CONFIGURED";
    case OAMR_CO_HB_WAITING: return "WAITING";
    case OAMR_CO_HB_ALIVE: return "ALIVE";
    case OAMR_CO_HB_LOST: return "LOST";
    default: return "INVALID";
    }
}
