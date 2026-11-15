/* SPDX-License-Identifier: MIT */
#include "oamr/ctl017_rx.h"

#include <string.h>

bool oamr_ctl017_rx_init(oamr_ctl017_rx_t *d, const oamr_ctl017_rx_config_t *cfg)
{
    memset(d, 0, sizeof(*d));
    if (cfg == 0 || cfg->node_id == 0u || cfg->node_id > 127u)
    {
        return false;
    }
    d->cfg = *cfg;
    d->configured = true;
    return true;
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

oamr_ctl017_rx_result_t oamr_ctl017_rx_on_frame(oamr_ctl017_rx_t *d, const oamr_co_frame_t *f,
                                                uint64_t now_us)
{
    if (!d->configured)
    {
        return OAMR_CTL017_RX_NOT_CONFIGURED;
    }
    if (f == 0 || f->extended || f->id > 0x7FFu || (f->id & 0x7Fu) != d->cfg.node_id)
    {
        d->counters.foreign++;
        return OAMR_CTL017_RX_FOREIGN;
    }
    const uint32_t fn = f->id & 0x780u;
    if (fn != OAMR_CTL017_COB_TPDO1_BASE && fn != OAMR_CTL017_COB_TPDO2_BASE &&
        fn != OAMR_CTL017_COB_EMCY_BASE)
    {
        d->counters.other++;
        return OAMR_CTL017_RX_OTHER;
    }
    if (f->dlc != 8u)
    {
        d->counters.malformed++;
        return OAMR_CTL017_RX_MALFORMED;
    }
    oamr_ctl017_rx_data_t *v = &d->data;
    const uint8_t *p = f->data;
    if (fn == OAMR_CTL017_COB_TPDO1_BASE)
    {
        if (v->status_seen && p[7] != (uint8_t)(v->status_counter + 1u))
        {
            d->counters.counter_gaps++;
        }
        v->status_seen = true;
        v->status_us = now_us;
        v->state = p[0];
        v->flags = p[1];
        v->position_raw = le16(&p[2]);
        v->active_stop_id = p[4];
        v->last_seq = p[5];
        v->cmd_result = p[6];
        v->status_counter = p[7];
        d->counters.status++;
        return OAMR_CTL017_RX_STATUS;
    }
    if (fn == OAMR_CTL017_COB_TPDO2_BASE)
    {
        v->status2_seen = true;
        v->status2_us = now_us;
        v->inputs = p[0];
        v->target_stop_id = p[1];
        v->motor_current_ma = le16(&p[2]);
        v->velocity_raw = (int16_t)le16(&p[4]);
        v->faults_f01_f08 = p[6];
        v->aux_flags = p[7];
        d->counters.status2++;
        return OAMR_CTL017_RX_STATUS2;
    }
    v->emcy_seen = true;
    v->emcy_us = now_us;
    v->emcy_code = le16(&p[0]);
    v->emcy_error_register = p[2];
    v->emcy_fault_no = p[3];
    v->emcy_fault_mask = le16(&p[4]);
    d->counters.emcy++;
    return OAMR_CTL017_RX_EMCY;
}

bool oamr_ctl017_position_known(const oamr_ctl017_rx_t *d)
{
    return d->configured && d->data.status_seen &&
           (d->data.flags & OAMR_CTL017_FLAG_POSITION_VALID) != 0u &&
           d->data.position_raw != OAMR_CTL017_POSITION_UNKNOWN;
}

const char *oamr_ctl017_state_name(const oamr_ctl017_rx_t *d)
{
    static const char *const names[] = {"INIT",   "UNHOMED",  "HOMING", "IDLE",
                                        "MOVING", "STOPPING", "FAULT"};
    if (!d->configured || !d->data.status_seen || d->data.state >= sizeof(names) / sizeof(names[0]))
    {
        return "unknown";
    }
    return names[d->data.state];
}
