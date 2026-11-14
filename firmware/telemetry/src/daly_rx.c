/* SPDX-License-Identifier: MIT */
#include "oamr/daly_rx.h"

#include <string.h>

bool oamr_daly_rx_init(oamr_daly_rx_t *d, const oamr_daly_rx_config_t *cfg)
{
    memset(d, 0, sizeof(*d));
    if (cfg == NULL || !cfg->addresses_set || cfg->bms_addr == cfg->host_addr || cfg->n_cells == 0u ||
        cfg->n_cells > OAMR_DALY_MAX_CELLS || cfg->n_temps == 0u || cfg->n_temps > OAMR_DALY_MAX_TEMPS) {
        return false;
    }
    d->cfg = *cfg;
    d->configured = true;
    return true;
}

uint32_t oamr_daly_rx_expected_id(const oamr_daly_rx_t *d, uint8_t data_id)
{
    return ((uint32_t)OAMR_DALY_PRIORITY << 24) | ((uint32_t)data_id << 16) |
           ((uint32_t)d->cfg.host_addr << 8) | (uint32_t)d->cfg.bms_addr;
}

bool oamr_daly_rx_usable(const oamr_daly_rx_t *d)
{
    return d->configured && d->cfg.protocol_confirmed;
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int16_t temp_c(uint8_t raw)
{
    return (int16_t)((int16_t)raw - OAMR_DALY_TEMP_OFFSET);
}

static oamr_daly_rx_result_t malformed(oamr_daly_rx_t *d)
{
    d->counters.malformed++;
    return OAMR_DALY_RX_MALFORMED;
}

oamr_daly_rx_result_t oamr_daly_rx_on_frame(oamr_daly_rx_t *d, const oamr_daly_rx_frame_t *f,
                                            uint32_t now_us)
{
    if (!d->configured) {
        return OAMR_DALY_RX_NOT_CONFIGURED;
    }
    const uint32_t addr_mask = 0x1F00FFFFu;
    const uint32_t want = oamr_daly_rx_expected_id(d, 0u);
    if (!f->extended || (f->id & ~0x1FFFFFFFu) != 0u || (f->id & addr_mask) != want) {
        d->counters.foreign++;
        return OAMR_DALY_RX_FOREIGN;
    }
    const uint8_t data_id = (uint8_t)((f->id >> 16) & 0xFFu);
    if (data_id < OAMR_DALY_ID_SOC || data_id > OAMR_DALY_ID_FAILURE) {
        d->counters.unknown_id++;
        d->counters.unknown_id_last = data_id;
        return OAMR_DALY_RX_UNKNOWN_ID;
    }
    if (f->dlc != 8u) {
        return malformed(d);
    }
    const uint8_t *p = f->data;
    oamr_daly_rx_data_t *s = &d->data;

    switch (data_id) {
    case OAMR_DALY_ID_SOC:
        s->pack_voltage_v = (float)be16(&p[0]) * 0.1f;
        s->acquired_voltage_v = (float)be16(&p[2]) * 0.1f;
        s->current_a = (float)((int32_t)be16(&p[4]) - OAMR_DALY_CURRENT_OFFSET) * 0.1f;
        s->soc_percent = (float)be16(&p[6]) * 0.1f;
        s->soc_seen = true;
        s->soc_stamp_us = now_us;
        break;
    case OAMR_DALY_ID_CELL_MINMAX:
        if (p[2] == 0u || p[5] == 0u || p[2] > d->cfg.n_cells || p[5] > d->cfg.n_cells) {
            return malformed(d);
        }
        s->cell_max_mv = be16(&p[0]);
        s->cell_max_no = p[2];
        s->cell_min_mv = be16(&p[3]);
        s->cell_min_no = p[5];
        s->cell_minmax_seen = true;
        s->cell_minmax_stamp_us = now_us;
        break;
    case OAMR_DALY_ID_TEMP_MINMAX:
        if (p[1] == 0u || p[3] == 0u || p[1] > d->cfg.n_temps || p[3] > d->cfg.n_temps) {
            return malformed(d);
        }
        s->temp_max_c = temp_c(p[0]);
        s->temp_max_no = p[1];
        s->temp_min_c = temp_c(p[2]);
        s->temp_min_no = p[3];
        s->temp_minmax_seen = true;
        s->temp_minmax_stamp_us = now_us;
        break;
    case OAMR_DALY_ID_MOS:
        if (p[1] > 1u || p[2] > 1u) {
            return malformed(d);
        }
        s->charge_state = p[0];
        s->charge_mos_on = p[1] != 0u;
        s->discharge_mos_on = p[2] != 0u;
        s->bms_life = p[3];
        s->remaining_mah = be32(&p[4]);
        s->mos_seen = true;
        s->mos_stamp_us = now_us;
        break;
    case OAMR_DALY_ID_STATUS:
        if (p[2] > 1u || p[3] > 1u) {
            return malformed(d);
        }
        s->n_cells_reported = p[0];
        s->n_temps_reported = p[1];
        s->charger_present = p[2] != 0u;
        s->load_present = p[3] != 0u;
        s->dio_bits = p[4];
        s->cycles = be16(&p[5]);
        s->status_seen = true;
        s->status_stamp_us = now_us;
        break;
    case OAMR_DALY_ID_CELL_VOLTAGES: {
        /* byte 0 frame number from 1, then three cells of 2 bytes. */
        const uint8_t frame = p[0];
        if (frame == 0u || (uint32_t)(frame - 1u) * 3u >= d->cfg.n_cells) {
            return malformed(d);
        }
        for (uint32_t i = 0u; i < 3u; i++) {
            const uint32_t cell = (uint32_t)(frame - 1u) * 3u + i;
            if (cell < d->cfg.n_cells) {
                s->cell_mv[cell] = be16(&p[1u + 2u * i]);
                s->cell_seen_mask |= (uint64_t)1u << cell;
            }
        }
        break;
    }
    case OAMR_DALY_ID_CELL_TEMPS: {
        /* byte 0 frame number from 1, then seven temperatures. */
        const uint8_t frame = p[0];
        if (frame == 0u || (uint32_t)(frame - 1u) * 7u >= d->cfg.n_temps) {
            return malformed(d);
        }
        for (uint32_t i = 0u; i < 7u; i++) {
            const uint32_t t = (uint32_t)(frame - 1u) * 7u + i;
            if (t < d->cfg.n_temps) {
                s->cell_temp_c[t] = temp_c(p[1u + i]);
                s->temp_seen_mask |= (uint32_t)1u << t;
            }
        }
        break;
    }
    case OAMR_DALY_ID_BALANCE:
        memcpy(s->balance_raw, p, sizeof(s->balance_raw));
        s->balance_seen = true;
        break;
    case OAMR_DALY_ID_FAILURE: {
        memcpy(s->failure_raw, p, 8u);
        bool l1 = false;
        bool l2 = false;
        bool hw = false;
        for (unsigned i = 0u; i < 4u; i++) {
            l1 = l1 || (p[i] & OAMR_DALY_LEVEL1_MASK) != 0u;
            l2 = l2 || (p[i] & OAMR_DALY_LEVEL2_MASK) != 0u;
        }
        for (unsigned i = 4u; i < 7u; i++) {
            hw = hw || p[i] != 0u;
        }
        s->any_level1 = l1;
        s->any_level2 = l2;
        s->any_hw_failure = hw;
        s->failure_seen = true;
        s->failure_stamp_us = now_us;
        break;
    }
    default:
        /* 0x90..0x98 are all handled above. */
        break;
    }
    s->confirmed = d->cfg.protocol_confirmed;
    d->counters.decoded++;
    return OAMR_DALY_RX_DECODED;
}
