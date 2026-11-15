/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
#include "tharness.h"

#include <string.h>

int th_failures, th_checks, th_cases;

int th_report(const char *suite)
{
    fprintf(stderr, "%s: %d cases, %d checks, %d failures\n", suite, th_cases, th_checks, th_failures);
    if (th_cases == 0) {
        fprintf(stderr, "%s: no test case ran\n", suite);
        return 1;
    }
    return th_failures ? 1 : 0;
}

static th_world_t *g_w;
static uint32_t g_seq;
static uint32_t g_epoch = 7;

static void drive_tx(th_world_t *w, uint32_t id, const uint8_t *d, uint8_t len)
{
    simref_frame_t f = {.id = id, .len = len};
    memcpy(f.data, d, len);
    simref_ctrl_on_frame(&w->c, SIMREF_BUS_DRIVE, &f, w->t);
}

static void sdo_reply(th_world_t *w, uint8_t cs, uint16_t idx, uint8_t sub, uint32_t v)
{
    uint8_t b[8] = {cs, (uint8_t)idx, (uint8_t)(idx >> 8), sub, (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16),
                    (uint8_t)(v >> 24)};
    drive_tx(w, 0x581, b, 8);
}

static uint16_t statusword(const th_drive_t *d)
{
    if (d->fault)
        return 0x0018;
    return (uint16_t)(d->sw_state | 0x10);
}

static void fake_drive_rx(th_world_t *w, const simref_frame_t *f)
{
    th_drive_t *d = &w->d;
    if (d->silent)
        return;
    if (f->id == 0x000 && f->len == 2 && (f->data[1] == 1 || f->data[1] == 0)) {
        if (f->data[0] == 1) {
            d->nmt = 5;
            d->nmt_starts++;
        }
        return;
    }
    if (f->id != 0x601 || f->len != 8)
        return;
    uint8_t cs = f->data[0];
    uint16_t idx = (uint16_t)(f->data[1] | (f->data[2] << 8));
    uint8_t sub = f->data[3];
    int32_t v = (int32_t)((uint32_t)f->data[4] | ((uint32_t)f->data[5] << 8) | ((uint32_t)f->data[6] << 16) |
                          ((uint32_t)f->data[7] << 24));
    if (cs == 0x40) {
        uint32_t val = 0;
        uint8_t rcs = 0x4B;
        switch (idx) {
        case 0x2026: val = 1; break;
        case 0x1017: val = d->v1017; break;
        case 0x2000: val = d->v2000; break;
        case 0x6060: val = 3; rcs = 0x4F; break;
        case 0x603F: val = d->fault ? 0x4 : 0; rcs = 0x43; break;
        case 0x6041: val = statusword(d); break;
        case 0x606C: val = (uint32_t)(d->sw_state == 0x27 ? d->target[sub - 1] * 10 : 0); rcs = 0x43; break;
        default: sdo_reply(w, 0x80, idx, sub, 0x06020000u); return;
        }
        sdo_reply(w, rcs, idx, sub, val);
        return;
    }
    /* write */
    if (idx == 0x6040) {
        d->writes_6040++;
        if (d->abort_word >= 0 && (v & 0xFF) == d->abort_word) {
            sdo_reply(w, 0x80, idx, sub, 0x08000000u);
            return;
        }
        uint16_t next = d->sw_state;
        int cw = v & 0x8F;
        if (cw == 0x06) next = 0x21;
        else if (cw == 0x07) next = 0x23;
        else if (cw == 0x0F) {
            next = 0x27;
            w->controlword_0f_writes++;
        } else if (cw == 0x00) next = 0x40;
        if (d->transition_delay_us > 0) {
            d->pending_sw = next;
            d->pending_due_us = w->t + d->transition_delay_us;
        } else {
            d->sw_state = next;
        }
    } else if (idx == 0x60FF) {
        d->writes_60ff++;
        d->target[sub - 1] = v;
        if (v != 0)
            w->last_nonzero_target_us = w->t;
        else if (w->last_nonzero_target_us >= 0 && w->first_zero_after_nonzero_us < 0)
            w->first_zero_after_nonzero_us = w->t;
    }
    sdo_reply(w, 0x60, idx, sub, 0);
}

static void ctrl_send(void *ctx, simref_bus_t bus, const simref_frame_t *f)
{
    th_world_t *w = ctx;
    w->tx_count[bus]++;
    if (bus == SIMREF_BUS_DRIVE)
        fake_drive_rx(w, f);
}

void th_world_init(th_world_t *w, simref_cause_t cause)
{
    memset(w, 0, sizeof *w);
    g_w = w;
    w->d.nmt = 5;
    w->d.sw_state = 0x40;
    w->d.abort_word = -1;
    w->d.v1017 = 100;
    w->d.v2000 = 250;
    w->d.booted = false;
    w->last_nonzero_target_us = -1;
    w->first_zero_after_nonzero_us = -1;
    w->lift_alive = true;
    w->t = 1000000;
    simref_ctrl_cfg_t cfg;
    simref_ctrl_default_cfg(&cfg);
    cfg.boot_cause = cause;
    simref_ctrl_init(&w->c, &cfg, w->t, ctrl_send, w);
}

void th_drive_power_cycle(th_world_t *w)
{
    w->d.sw_state = 0x40;
    w->d.target[0] = w->d.target[1] = 0;
    w->d.booted = false;
    w->d.next_hb_us = w->t;
}

void th_world_run(th_world_t *w, int64_t dt)
{
    int64_t end = w->t + dt;
    while (w->t < end) {
        w->t += 1000;
        th_drive_t *d = &w->d;
        if (d->pending_due_us && w->t >= d->pending_due_us) {
            d->sw_state = d->pending_sw;
            d->pending_due_us = 0;
        }
        if (!d->silent && !d->booted) {
            d->booted = true;
            uint8_t b = 0x00;
            drive_tx(w, 0x701, &b, 1);
            d->next_hb_us = w->t + 100000;
        }
        if (!d->silent && w->t >= d->next_hb_us) {
            d->next_hb_us = w->t + 100000;
            drive_tx(w, 0x701, &d->nmt, 1);
        }
        if (w->lift_alive && w->t >= w->lift_next_hb) {
            w->lift_next_hb = w->t + 100000;
            simref_frame_t f = {.id = 0x710, .len = 1, .data = {0x05}};
            simref_ctrl_on_frame(&w->c, SIMREF_BUS_AUX, &f, w->t);
        }
        simref_ctrl_step(&w->c, w->t);
    }
}

void th_world_pins(th_world_t *w, int y32, int estop, int reset, int cf, int cont)
{
    int v[5] = {y32, estop, reset, cf, cont};
    for (int i = 0; i < 5; i++)
        simref_ctrl_set_pin(&w->c, (simref_pin_t)i, v[i] != 0, w->t);
}

void th_world_pin(th_world_t *w, simref_pin_t p, int level) { simref_ctrl_set_pin(&w->c, p, level != 0, w->t); }

void th_world_host_hb(th_world_t *w) { simref_ctrl_host_heartbeat(&w->c, g_epoch, w->t); }

void th_world_cmd(th_world_t *w, double v, double om)
{
    simref_ctrl_host_heartbeat(&w->c, g_epoch, w->t);
    simref_ctrl_host_cmd(&w->c, g_epoch, ++g_seq, v, om, w->t);
}

void th_world_press_reset(th_world_t *w, int64_t hold_ms)
{
    th_world_pin(w, SIMREF_PIN_RESET, 1);
    for (int64_t k = 0; k < hold_ms; k += 50) {
        th_world_host_hb(w);
        th_world_run(w, 50000);
    }
    th_world_pin(w, SIMREF_PIN_RESET, 0);
    th_world_run(w, 50000);
}

void th_world_arm_to_run(th_world_t *w)
{
    th_world_pins(w, 1, 0, 0, 0, 1);
    simref_ctrl_host_session(&w->c, g_epoch, w->t);
    th_world_run(w, 300000); /* drive heartbeat, boot read */
    th_world_host_hb(w);
    simref_ctrl_host_supervisory(&w->c, SIMREF_SUP_JETSON_STOP_RELEASE, g_epoch, ++g_seq, w->t);
    th_world_run(w, 20000);
    th_world_press_reset(w, 400);
}

bool th_reason_logged(const simref_ctrl_t *c, simref_reason_t r)
{
    uint32_t first = c->log_n > SIMREF_CTRL_LOG ? c->log_n - SIMREF_CTRL_LOG : 0;
    for (uint32_t i = first; i < c->log_n; i++)
        if (c->log[i % SIMREF_CTRL_LOG].reason == r)
            return true;
    return false;
}
