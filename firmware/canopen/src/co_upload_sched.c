/* SPDX-License-Identifier: MIT */
#include "oamr/co_upload_sched.h"

#include <string.h>

#include "oamr/co_cobid.h"

oamr_co_upl_state_t oamr_co_upl_init(oamr_co_upl_t *s, const oamr_co_upl_config_t *cfg)
{
    memset(s, 0, sizeof(*s));
    s->state = OAMR_CO_UPL_NOT_CONFIGURED;
    if (cfg == 0 || !oamr_co_node_id_valid(cfg->node_id) || cfg->objects == 0 ||
        cfg->n_objects == 0u || cfg->n_objects > OAMR_CO_UPL_MAX_ENTRIES || cfg->timeout_us == 0u ||
        !cfg->max_retries_set || cfg->send == 0) {
        return s->state;
    }
    s->cfg = *cfg;
    s->state = OAMR_CO_UPL_IDLE;
    return s->state;
}

static void advance(oamr_co_upl_t *s)
{
    s->attempt = 0;
    s->state = OAMR_CO_UPL_IDLE;
    s->cur++;
    if (s->cur >= s->cfg.n_objects) {
        s->cur = 0;
        s->cycles++;
    }
}

static void fail_current(oamr_co_upl_t *s, oamr_co_sample_status_t why)
{
    oamr_co_sample_t *smp = &s->samples[s->cur];
    smp->status = why;
    smp->value = 0; /* never leave an old value next to a non-valid status */
    smp->size = 0;
    smp->size_indicated = false;
    s->counters[s->cur].failed++;
    advance(s);
}

static void send_request(oamr_co_upl_t *s, uint64_t now_us)
{
    const oamr_co_object_t *o = &s->cfg.objects[s->cur];
    oamr_co_frame_t f;
    (void)oamr_co_sdo_encode_upload_request(s->cfg.node_id, o->index, o->subindex, &f);
    s->attempt++;
    if (!s->cfg.send(s->cfg.send_ctx, &f)) {
        s->counters[s->cur].tx_failures++;
        if (s->attempt > s->cfg.max_retries) {
            fail_current(s, OAMR_CO_SAMPLE_TX_FAILED);
        } else {
            s->counters[s->cur].retries++;
            s->state = OAMR_CO_UPL_IDLE; /* try again on the next tick */
        }
        return;
    }
    s->sent_us = now_us;
    s->state = OAMR_CO_UPL_BUSY;
}

void oamr_co_upl_tick(oamr_co_upl_t *s, uint64_t now_us)
{
    switch (s->state) {
    case OAMR_CO_UPL_NOT_CONFIGURED:
        return;
    case OAMR_CO_UPL_IDLE:
        send_request(s, now_us);
        return;
    case OAMR_CO_UPL_BUSY:
        /* A clock that went backwards counts as a timeout: never wait forever. */
        if (now_us < s->sent_us || now_us - s->sent_us >= s->cfg.timeout_us) {
            s->counters[s->cur].timeouts++;
            if (s->attempt > s->cfg.max_retries) {
                fail_current(s, OAMR_CO_SAMPLE_TIMEOUT);
            } else {
                s->counters[s->cur].retries++;
                send_request(s, now_us);
            }
        }
        return;
    default:
        return;
    }
}

bool oamr_co_upl_on_frame(oamr_co_upl_t *s, const oamr_co_frame_t *f, uint64_t now_us)
{
    if (s->state == OAMR_CO_UPL_NOT_CONFIGURED || f == 0 || f->extended ||
        f->id != OAMR_CO_COB_SDO_TX_BASE + s->cfg.node_id) {
        return false;
    }
    if (s->state != OAMR_CO_UPL_BUSY) {
        s->unsolicited++; /* late answer after a timeout, or a duplicate */
        return true;
    }
    const oamr_co_object_t *o = &s->cfg.objects[s->cur];
    oamr_co_sdo_response_t r;
    const oamr_co_sdo_rx_t rx = oamr_co_sdo_decode_upload_response(f, s->cfg.node_id, o->index,
                                                                   o->subindex, &r);
    oamr_co_sample_t *smp = &s->samples[s->cur];
    switch (rx) {
    case OAMR_CO_SDO_RX_OK:
        smp->status = OAMR_CO_SAMPLE_VALID;
        smp->value = r.value;
        smp->size = r.size;
        smp->size_indicated = r.size_indicated;
        smp->sample_us = now_us;
        smp->seq++;
        s->counters[s->cur].ok++;
        advance(s);
        return true;
    case OAMR_CO_SDO_RX_ABORT:
        smp->abort_code = r.abort_code;
        s->counters[s->cur].aborts++;
        fail_current(s, OAMR_CO_SAMPLE_ABORTED);
        return true;
    case OAMR_CO_SDO_RX_WRONG_OBJECT:
        s->wrong_object++; /* keep waiting for our own answer */
        return true;
    case OAMR_CO_SDO_RX_MALFORMED:
    case OAMR_CO_SDO_RX_SEGMENTED:
    case OAMR_CO_SDO_RX_UNEXPECTED_CS:
        s->counters[s->cur].protocol_errors++;
        fail_current(s, OAMR_CO_SAMPLE_PROTOCOL);
        return true;
    case OAMR_CO_SDO_RX_NOT_FOR_US:
    default:
        return false;
    }
}

const oamr_co_sample_t *oamr_co_upl_sample(const oamr_co_upl_t *s, uint8_t i)
{
    if (s->state == OAMR_CO_UPL_NOT_CONFIGURED || i >= s->cfg.n_objects) {
        return 0;
    }
    return &s->samples[i];
}

const oamr_co_upl_counters_t *oamr_co_upl_counters(const oamr_co_upl_t *s, uint8_t i)
{
    if (s->state == OAMR_CO_UPL_NOT_CONFIGURED || i >= s->cfg.n_objects) {
        return 0;
    }
    return &s->counters[i];
}
