/* SPDX-License-Identifier: MIT */
#include "oamr/daly_bms.h"

#include <string.h>

/* Data IDs that must all be fresh for the status to count as fresh. */
static const uint8_t k_required[5] = {
    OAMR_DALY_ID_PACK, OAMR_DALY_ID_CELL_MINMAX, OAMR_DALY_ID_TEMP_MINMAX,
    OAMR_DALY_ID_MOS, OAMR_DALY_ID_FAILURE,
};
#define N_REQUIRED (sizeof(k_required) / sizeof(k_required[0]))

void oamr_daly_default_config(oamr_daly_config_t *cfg)
{
    cfg->request_period_us = 40000u;   /* five IDs -> each at 5 Hz (I8 WP: BMS 5 Hz minimum) */
    cfg->stale_us = 3000000u;          /* TODO(Evans) */
}

void oamr_daly_init(oamr_daly_t *d, const oamr_daly_config_t *cfg, const oamr_can_tx_t *tx)
{
    memset(d, 0, sizeof(*d));
    d->cfg = *cfg;
    d->tx = *tx;
}

uint32_t oamr_daly_request_id(uint8_t data_id)
{
    return (0x18u << 24) | ((uint32_t)data_id << 16) | (OAMR_DALY_BMS_ADDR << 8) | OAMR_DALY_HOST_ADDR;
}

uint32_t oamr_daly_response_id(uint8_t data_id)
{
    return (0x18u << 24) | ((uint32_t)data_id << 16) | (OAMR_DALY_HOST_ADDR << 8) | OAMR_DALY_BMS_ADDR;
}

void oamr_daly_poll(oamr_daly_t *d, uint64_t now_us)
{
    if (d->requested_once && now_us - d->last_request_us < d->cfg.request_period_us) {
        return;
    }
    oamr_can_frame_t f;
    memset(&f, 0, sizeof(f));
    f.id = oamr_daly_request_id(k_required[d->next]);
    f.extended = true;
    f.dlc = 8;
    if (!oamr_can_send(&d->tx, &f)) {
        d->tx_failures++;
    }
    d->requested_once = true;
    d->last_request_us = now_us;
    d->next = (uint8_t)((d->next + 1u) % N_REQUIRED);
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void update_fault(oamr_bms_status_t *s)
{
    bool fault = false;
    bool warning = false;
    for (size_t i = 0; i < 4; i++) {
        fault = fault || (s->raw_failure[i] & OAMR_DALY_LEVEL2_MASK) != 0u;
        warning = warning || (s->raw_failure[i] & OAMR_DALY_LEVEL1_MASK) != 0u;
    }
    for (size_t i = 4; i < 7; i++) {
        fault = fault || s->raw_failure[i] != 0u;
    }
    s->fault = fault;
    s->warning = warning;
}

bool oamr_daly_on_frame(oamr_daly_t *d, const oamr_can_frame_t *f, uint64_t now_us)
{
    if (!f->extended || (f->id & 0xFF00FFFFu) != ((0x18u << 24) | (OAMR_DALY_HOST_ADDR << 8) | OAMR_DALY_BMS_ADDR)) {
        return false;
    }
    if (f->dlc != 8) {
        d->frames_ignored++;
        return true;
    }
    const uint8_t id = (uint8_t)((f->id >> 16) & 0xFFu);
    const uint8_t *p = f->data;
    oamr_bms_status_t *s = &d->status;
    switch (id) {
    case OAMR_DALY_ID_PACK:
        s->pack_voltage_v = (float)be16(&p[0]) * 0.1f;
        s->pack_current_a = ((float)be16(&p[4]) - 30000.0f) * 0.1f;
        s->soc_percent = (float)be16(&p[6]) * 0.1f;
        break;
    case OAMR_DALY_ID_CELL_MINMAX:
        s->cell_mv_max = be16(&p[0]);
        s->cell_mv_min = be16(&p[3]);
        break;
    case OAMR_DALY_ID_TEMP_MINMAX:
        s->temp_c_max = (int16_t)((int16_t)p[0] - 40);
        s->temp_c_min = (int16_t)((int16_t)p[2] - 40);
        break;
    case OAMR_DALY_ID_MOS:
        s->charge_mos_on = p[1] != 0u;
        s->discharge_mos_on = p[2] != 0u;
        break;
    case OAMR_DALY_ID_FAILURE:
        memcpy(s->raw_failure, p, 8);
        update_fault(s);
        break;
    default:
        d->frames_ignored++;
        return true;
    }
    d->frames_rx++;
    for (size_t i = 0; i < N_REQUIRED; i++) {
        if (k_required[i] == id) {
            d->rx_us[i] = now_us;
            d->rx_seen[i] = true;
        }
    }
    bool all = true;
    uint64_t oldest = UINT64_MAX;
    for (size_t i = 0; i < N_REQUIRED; i++) {
        all = all && d->rx_seen[i];
        if (d->rx_seen[i] && d->rx_us[i] < oldest) {
            oldest = d->rx_us[i];
        }
    }
    s->have_data = all;
    s->stamp_us = all ? oldest : 0u;
    return true;
}

bool oamr_daly_fresh(const oamr_daly_t *d, uint64_t now_us)
{
    for (size_t i = 0; i < N_REQUIRED; i++) {
        if (!d->rx_seen[i] || now_us < d->rx_us[i] || now_us - d->rx_us[i] >= d->cfg.stale_us) {
            return false;
        }
    }
    return true;
}

const oamr_bms_status_t *oamr_daly_status(const oamr_daly_t *d)
{
    return &d->status;
}
