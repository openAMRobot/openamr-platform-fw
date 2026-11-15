/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
#include "simref/simref_drive.h"

#include <string.h>

#define SW_MASK_STATE 0x006Fu
#define SW_RTSO 0x0021u
#define SW_SO 0x0023u
#define SW_OE 0x0027u
#define SW_SOD_MASK 0x004Fu
#define SW_SOD 0x0040u
#define SW_FAULT 0x0008u

static const char *const PHASE_NAMES[SIMREF_DL_COUNT] = {"ABSENT", "BOOT_READ", "IDLE", "ENABLING",
                                                         "ENABLED", "FAILED", "LOST"};
static const char *const DETAIL_NAMES[SIMREF_DD_COUNT] = {
    "NONE", "HB_TIMEOUT", "DRIVE_RESTARTED", "SDO_TIMEOUT", "SDO_ABORT", "TRANSITION_TIMEOUT",
    "CONFIG_MISMATCH", "DRIVE_FAULT", "NOT_ENABLED", "FOUND_ENABLED_AT_BOOT", "ENCODER_READ_FAILED"};

const char *simref_dl_phase_name(simref_dl_phase_t p) { return (unsigned)p < SIMREF_DL_COUNT ? PHASE_NAMES[p] : "?"; }
const char *simref_dl_detail_name(simref_dl_detail_t d) { return (unsigned)d < SIMREF_DD_COUNT ? DETAIL_NAMES[d] : "?"; }

static void push_event(simref_drive_t *d, simref_dl_event_t e, simref_dl_detail_t det)
{
    d->detail = det;
    if (d->n_ev < SIMREF_DRIVE_MAX_EVENTS) {
        d->ev[d->n_ev] = e;
        d->ev_detail[d->n_ev] = det;
        d->n_ev++;
    }
}

simref_dl_event_t simref_drive_pop_event(simref_drive_t *d, simref_dl_detail_t *detail)
{
    if (d->n_ev == 0)
        return SIMREF_DLE_NONE;
    simref_dl_event_t e = d->ev[0];
    if (detail)
        *detail = d->ev_detail[0];
    for (int i = 1; i < d->n_ev; i++) {
        d->ev[i - 1] = d->ev[i];
        d->ev_detail[i - 1] = d->ev_detail[i];
    }
    d->n_ev--;
    return e;
}

void simref_drive_init(simref_drive_t *d, const simref_drive_cfg_t *cfg, simref_send_fn send, void *ctx)
{
    memset(d, 0, sizeof *d);
    d->cfg = *cfg;
    d->send = send;
    d->send_ctx = ctx;
    d->phase = SIMREF_DL_ABSENT;
}

static void tx(simref_drive_t *d, uint32_t id, const uint8_t *data, uint8_t len)
{
    simref_frame_t f = {.id = id, .len = len};
    memcpy(f.data, data, len);
    if (d->send)
        d->send(d->send_ctx, SIMREF_BUS_DRIVE, &f);
}

static void sdo_start(simref_drive_t *d, uint16_t index, uint8_t sub, bool write, uint8_t size, int32_t value,
                      int64_t now)
{
    uint8_t b[8] = {0};
    if (write) {
        b[0] = size == 1 ? 0x2F : size == 2 ? 0x2B : 0x23;
        uint32_t u = (uint32_t)value;
        for (int i = 0; i < 4; i++)
            b[4 + i] = (uint8_t)(u >> (8 * i));
        if (index == 0x60FF)
            d->target_writes++;
    } else {
        b[0] = 0x40;
    }
    b[1] = (uint8_t)(index & 0xFF);
    b[2] = (uint8_t)(index >> 8);
    b[3] = sub;
    d->sdo_op = (simref_sdo_op_t){.index = index, .sub = sub, .write = write, .size = size, .value = value};
    d->sdo_busy = true;
    d->sdo_sent_us = now;
    tx(d, 0x600u + d->cfg.node_id, b, 8);
}

static void shutdown_word(simref_drive_t *d, int64_t now)
{
    /* best effort, no wait: zero targets, then controlword 06h (shutdown, drive disabled) */
    uint8_t z1[8] = {0x23, 0xFF, 0x60, 0x01, 0, 0, 0, 0};
    uint8_t z2[8] = {0x23, 0xFF, 0x60, 0x02, 0, 0, 0, 0};
    uint8_t b[8] = {0x2B, 0x40, 0x60, 0x00, 0x06, 0, 0, 0};
    (void)now;
    tx(d, 0x600u + d->cfg.node_id, z1, 8);
    tx(d, 0x600u + d->cfg.node_id, z2, 8);
    tx(d, 0x600u + d->cfg.node_id, b, 8);
    d->sent_rpm[0] = d->sent_rpm[1] = 0;
    d->sdo_busy = false;
}

static void boot_next(simref_drive_t *d, int64_t now);

static void enter(simref_drive_t *d, simref_dl_phase_t p, int64_t now)
{
    d->phase = p;
    if (p == SIMREF_DL_BOOT_READ) {
        d->boot_ok = false;
        d->detail = SIMREF_DD_NONE;
    }
    d->step = (p == SIMREF_DL_IDLE || p == SIMREF_DL_ENABLED) ? -1 : 0;
    d->step_t0_us = now;
    d->next_cycle_us = now;
    d->sdo_busy = false;
    if (p != SIMREF_DL_ENABLED) {
        d->sent_rpm[0] = d->sent_rpm[1] = 0;
        d->act_valid[0] = d->act_valid[1] = false;
    }
}

static void fail(simref_drive_t *d, simref_dl_event_t e, simref_dl_detail_t det, int64_t now)
{
    shutdown_word(d, now);
    enter(d, SIMREF_DL_FAILED, now);
    d->enable_requested = false;
    push_event(d, e, det);
}

bool simref_drive_enabled(const simref_drive_t *d) { return d->phase == SIMREF_DL_ENABLED; }

bool simref_drive_request_enable(simref_drive_t *d, int64_t now)
{
    if (d->phase == SIMREF_DL_FAILED && !d->boot_ok) {
        enter(d, SIMREF_DL_BOOT_READ, now);
        d->enable_requested = true;
        boot_next(d, now);
        return true;
    }
    if (d->phase == SIMREF_DL_IDLE || d->phase == SIMREF_DL_FAILED) {
        enter(d, SIMREF_DL_ENABLING, now);
        d->enable_sequences++;
        d->nmt_sent = false;
        return true;
    }
    return false;
}

void simref_drive_disable(simref_drive_t *d, int64_t now)
{
    if (d->phase == SIMREF_DL_ENABLED || d->phase == SIMREF_DL_ENABLING) {
        shutdown_word(d, now);
        enter(d, SIMREF_DL_IDLE, now);
    }
}

void simref_drive_set_targets(simref_drive_t *d, int32_t l, int32_t r)
{
    if ((l == 0 && d->sent_rpm[0] != 0) || (r == 0 && d->sent_rpm[1] != 0))
        d->urgent_zero = true;
    d->target_rpm[0] = l;
    d->target_rpm[1] = r;
}

/* ---- sequencer ----------------------------------------------------------------------- */
/* Boot read steps R2..R7 (R1 is the heartbeat). */
static const struct { uint16_t index; uint8_t sub; } BOOT_READS[] = {
    {0x2026, 5}, {0x1017, 0}, {0x2000, 0}, {0x6060, 0}, {0x603F, 0}, {0x6041, 0}};
#define N_BOOT_READS 6
/* After a drive found enabled: E1, E2 zero targets and 6040h = 06h, acknowledged. */
#define BOOT_SAFE_FIRST 100

static bool boot_value_ok(const simref_drive_t *d, int idx, int32_t v)
{
    switch (idx) {
    case 0: return v == 1;
    case 1: return v == d->cfg.expect_1017;
    case 2: return v == d->cfg.expect_2000;
    case 3: return v == 3;
    case 4: return v == 0;
    default: return true;
    }
}

static void boot_next(simref_drive_t *d, int64_t now)
{
    if (d->step < N_BOOT_READS) {
        sdo_start(d, BOOT_READS[d->step].index, BOOT_READS[d->step].sub, false, 0, 0, now);
    } else if (d->step == BOOT_SAFE_FIRST) {
        sdo_start(d, 0x60FF, 1, true, 4, 0, now);
    } else if (d->step == BOOT_SAFE_FIRST + 1) {
        sdo_start(d, 0x60FF, 2, true, 4, 0, now);
    } else if (d->step == BOOT_SAFE_FIRST + 2) {
        sdo_start(d, 0x6040, 0, true, 2, 0x06, now);
    }
}

static void boot_done(simref_drive_t *d, int64_t now)
{
    d->epoch++;
    d->boot_ok = true;
    enter(d, SIMREF_DL_IDLE, now);
    if (d->enable_requested) {
        d->enable_requested = false;
        simref_drive_request_enable(d, now);
    }
}

/* Enable steps: 0 E1, 1 E2, 2 E3 NMT, 3 wait operational, 4 E4 write, 5 E4 poll, 6 E5 write,
 * 7 E5 poll, 8 E6 write, 9 E6 poll. */
static const uint16_t ENABLE_WORD[3] = {0x06, 0x07, 0x0F};
static const uint16_t ENABLE_SW[3] = {SW_RTSO, SW_SO, SW_OE};

static void enable_next(simref_drive_t *d, int64_t now)
{
    switch (d->step) {
    case 0: sdo_start(d, 0x60FF, 1, true, 4, 0, now); break;
    case 1: sdo_start(d, 0x60FF, 2, true, 4, 0, now); break;
    case 2: {
        uint8_t b[2] = {0x01, d->cfg.node_id};
        tx(d, 0x000, b, 2);
        d->nmt_starts++;
        d->step = 3;
        d->step_t0_us = now;
        break;
    }
    case 3:
        if (d->hb_state == 0x05) {
            d->step = 4;
            enable_next(d, now);
        } else if (now - d->step_t0_us >= d->cfg.hb_consumer_us) {
            fail(d, SIMREF_DLE_COMMS, SIMREF_DD_TRANSITION_TIMEOUT, now);
        }
        break;
    case 4: case 6: case 8:
        sdo_start(d, 0x6040, 0, true, 2, ENABLE_WORD[(d->step - 4) / 2], now);
        break;
    case 5: case 7: case 9:
        if (now - d->step_t0_us >= d->cfg.transition_limit_us)
            fail(d, SIMREF_DLE_COMMS, SIMREF_DD_TRANSITION_TIMEOUT, now);
        else
            sdo_start(d, 0x6041, 0, false, 0, 0, now);
        break;
    default: break;
    }
}

/* Cyclic steps: 0 W 60FF:1, 1 W 60FF:2, 2 R 606C:1, 3 R 606C:2, 4 R 6041, 5 R 603F. */
static void cycle_next(simref_drive_t *d, int64_t now)
{
    switch (d->step) {
    case 0: d->sent_rpm[0] = d->target_rpm[0]; sdo_start(d, 0x60FF, 1, true, 4, d->sent_rpm[0], now); break;
    case 1: d->sent_rpm[1] = d->target_rpm[1]; sdo_start(d, 0x60FF, 2, true, 4, d->sent_rpm[1], now); break;
    case 2: sdo_start(d, 0x606C, 1, false, 0, 0, now); break;
    case 3: sdo_start(d, 0x606C, 2, false, 0, 0, now); break;
    case 4: sdo_start(d, 0x6041, 0, false, 0, 0, now); break;
    case 5: sdo_start(d, 0x603F, 0, false, 0, 0, now); break;
    default: d->step = -1; break; /* cycle finished */
    }
}

static void sdo_done(simref_drive_t *d, bool ok, uint32_t abort, int32_t value, int64_t now)
{
    d->sdo_busy = false;
    if (!ok) {
        d->last_abort = abort;
        d->last_abort_index = d->sdo_op.index;
    }
    switch (d->phase) {
    case SIMREF_DL_BOOT_READ:
        if (!ok) {
            fail(d, SIMREF_DLE_CONFIG, abort ? SIMREF_DD_SDO_ABORT : SIMREF_DD_SDO_TIMEOUT, now);
            return;
        }
        if (d->step < N_BOOT_READS) {
            if (!boot_value_ok(d, d->step, value)) {
                fail(d, SIMREF_DLE_CONFIG, SIMREF_DD_CONFIG_MISMATCH, now);
                return;
            }
            if (d->step == 4)
                d->fault_code = (uint32_t)value;
            if (d->step == N_BOOT_READS - 1) {
                d->statusword = (uint16_t)value;
                if (((uint32_t)value & SW_MASK_STATE) == SW_OE) {
                    /* Found enabled at our boot: zero targets and disable before anything else.
                     * Interpretation, not an owner ruling. */
                    d->detail = SIMREF_DD_FOUND_ENABLED_AT_BOOT;
                    d->step = BOOT_SAFE_FIRST;
                    boot_next(d, now);
                    return;
                }
                if (((uint32_t)value & SW_SOD_MASK) != SW_SOD && ((uint32_t)value & SW_MASK_STATE) != SW_RTSO) {
                    fail(d, ((uint32_t)value & SW_FAULT) ? SIMREF_DLE_FAULT : SIMREF_DLE_CONFIG,
                         ((uint32_t)value & SW_FAULT) ? SIMREF_DD_DRIVE_FAULT : SIMREF_DD_CONFIG_MISMATCH, now);
                    return;
                }
                boot_done(d, now);
                return;
            }
            d->step++;
            boot_next(d, now);
        } else if (d->step < BOOT_SAFE_FIRST + 2) {
            d->step++;
            boot_next(d, now);
        } else {
            boot_done(d, now);
        }
        return;
    case SIMREF_DL_ENABLING:
        if (!ok) {
            fail(d, abort ? SIMREF_DLE_CONFIG : SIMREF_DLE_COMMS, abort ? SIMREF_DD_SDO_ABORT : SIMREF_DD_SDO_TIMEOUT,
                 now);
            return;
        }
        if (d->step == 0 || d->step == 1) {
            d->step++;
            enable_next(d, now);
        } else if (d->step == 4 || d->step == 6 || d->step == 8) {
            d->step++;
            d->step_t0_us = now;
            enable_next(d, now);
        } else if (d->step == 5 || d->step == 7 || d->step == 9) {
            d->statusword = (uint16_t)value;
            if ((uint32_t)value & SW_FAULT) {
                fail(d, SIMREF_DLE_FAULT, SIMREF_DD_DRIVE_FAULT, now);
                return;
            }
            if (((uint32_t)value & SW_MASK_STATE) == ENABLE_SW[(d->step - 5) / 2]) {
                if (d->step == 9) {
                    enter(d, SIMREF_DL_ENABLED, now);
                    d->detail = SIMREF_DD_NONE;
                    d->step = -1;
                    d->next_cycle_us = now;
                    return;
                }
                d->step++;
                enable_next(d, now);
            }
            /* else: poll again from step(), the limit is checked there */
        }
        return;
    case SIMREF_DL_IDLE:
        /* read-only evidence cycle while disabled: 606Ch only */
        if (d->step == 2 || d->step == 3) {
            int ch = d->step - 2;
            d->act_valid[ch] = ok;
            if (ok) {
                d->act_rpm10[ch] = value;
                d->act_rx_us[ch] = now;
                d->act_seq[ch]++;
            }
            d->step = d->step == 2 ? 3 : -1;
            if (d->step == 3)
                cycle_next(d, now);
        }
        return;
    case SIMREF_DL_ENABLED:
        if (!ok && (d->step == 2 || d->step == 3)) {
            int ch = d->step - 2;
            d->act_valid[ch] = false; /* channel READ_FAILED; "both wheels stopped" cannot be claimed */
            d->detail = SIMREF_DD_ENCODER_READ_FAILED;
            d->step++;
            cycle_next(d, now);
            return;
        }
        if (!ok) {
            fail(d, abort ? SIMREF_DLE_CONFIG : SIMREF_DLE_COMMS, abort ? SIMREF_DD_SDO_ABORT : SIMREF_DD_SDO_TIMEOUT,
                 now);
            return;
        }
        if (d->step == 2 || d->step == 3) {
            int ch = d->step - 2;
            d->act_rpm10[ch] = value;
            d->act_valid[ch] = true;
            d->act_rx_us[ch] = now;
            d->act_seq[ch]++;
        } else if (d->step == 4) {
            d->statusword = (uint16_t)value;
            if ((uint32_t)value & SW_FAULT) {
                fail(d, SIMREF_DLE_FAULT, SIMREF_DD_DRIVE_FAULT, now);
                return;
            }
            if (((uint32_t)value & SW_MASK_STATE) != SW_OE) {
                fail(d, SIMREF_DLE_COMMS, SIMREF_DD_NOT_ENABLED, now);
                return;
            }
        } else if (d->step == 5) {
            d->fault_code = (uint32_t)value;
        }
        d->step++;
        if (d->step == 4 && (d->cycle % (d->cfg.status_every ? d->cfg.status_every : 1u)) != 0)
            d->step = 6;
        cycle_next(d, now);
        return;
    default:
        return;
    }
}

void simref_drive_on_frame(simref_drive_t *d, const simref_frame_t *f, int64_t now)
{
    if (f->id == 0x700u + d->cfg.node_id && f->len >= 1) {
        uint8_t st = f->data[0];
        bool was_seen = d->hb_seen;
        d->hb_seen = true;
        d->hb_last_us = now;
        d->hb_state = st;
        if (st == 0x00) {
            /* boot-up: the drive restarted. Treat as lost; full boot read before re-enable. */
            if (d->phase == SIMREF_DL_ENABLED || d->phase == SIMREF_DL_ENABLING)
                push_event(d, SIMREF_DLE_COMMS, SIMREF_DD_DRIVE_RESTARTED);
            d->enable_requested = false;
            d->hb_state = 0x7F; /* pre-operational after boot-up */
            enter(d, SIMREF_DL_BOOT_READ, now);
            boot_next(d, now);
        } else if (!was_seen || d->phase == SIMREF_DL_LOST || d->phase == SIMREF_DL_ABSENT) {
            enter(d, SIMREF_DL_BOOT_READ, now);
            boot_next(d, now);
        }
        return;
    }
    if (f->id == 0x580u + d->cfg.node_id && f->len == 8 && d->sdo_busy) {
        uint8_t cs = f->data[0];
        uint16_t index = (uint16_t)(f->data[1] | (f->data[2] << 8));
        uint8_t sub = f->data[3];
        if (index != d->sdo_op.index || sub != d->sdo_op.sub)
            return;
        uint32_t raw = (uint32_t)f->data[4] | ((uint32_t)f->data[5] << 8) | ((uint32_t)f->data[6] << 16) |
                       ((uint32_t)f->data[7] << 24);
        if (cs == 0x80) {
            sdo_done(d, false, raw ? raw : 1u, 0, now);
        } else if (d->sdo_op.write && cs == 0x60) {
            sdo_done(d, true, 0, 0, now);
        } else if (!d->sdo_op.write && (cs & 0xF3u) == 0x43u) {
            unsigned n = 4u - ((cs >> 2) & 3u);
            int32_t v;
            if (n == 1)
                v = (int32_t)(int8_t)(raw & 0xFF);
            else if (n == 2)
                v = (int32_t)(int16_t)(raw & 0xFFFF);
            else
                v = (int32_t)raw;
            if (n == 1 || n == 2) {
                /* unsigned objects: keep the raw value except 6060h and 606Ch (signed) */
                if (d->sdo_op.index != 0x6060 && d->sdo_op.index != 0x606C)
                    v = (int32_t)(n == 1 ? (raw & 0xFF) : (raw & 0xFFFF));
            }
            sdo_done(d, true, 0, v, now);
        }
    }
}

int64_t simref_drive_next_deadline(const simref_drive_t *d)
{
    int64_t t = INT64_MAX;
    if (d->hb_seen && d->phase != SIMREF_DL_LOST && d->phase != SIMREF_DL_ABSENT)
        t = d->hb_last_us + d->cfg.hb_consumer_us;
    if (d->sdo_busy && d->sdo_sent_us + d->cfg.sdo_timeout_us < t)
        t = d->sdo_sent_us + d->cfg.sdo_timeout_us;
    if ((d->phase == SIMREF_DL_ENABLED || d->phase == SIMREF_DL_IDLE) && !d->sdo_busy && d->step < 0 &&
        d->next_cycle_us < t)
        t = d->next_cycle_us;
    if (d->phase == SIMREF_DL_ENABLING && !d->sdo_busy && d->step_t0_us + d->cfg.transition_limit_us < t)
        t = d->step_t0_us + 1000; /* polling */
    return t;
}

void simref_drive_step(simref_drive_t *d, int64_t now)
{
    /* heartbeat consumer: expired when the age reaches the consumer time */
    if (d->hb_seen && d->phase != SIMREF_DL_LOST && d->phase != SIMREF_DL_ABSENT &&
        now - d->hb_last_us >= d->cfg.hb_consumer_us) {
        enter(d, SIMREF_DL_LOST, now);
        d->enable_requested = false;
        push_event(d, SIMREF_DLE_COMMS, SIMREF_DD_HB_TIMEOUT);
        return;
    }
    if (d->sdo_busy) {
        if (now - d->sdo_sent_us >= d->cfg.sdo_timeout_us)
            sdo_done(d, false, 0, 0, now);
        return;
    }
    switch (d->phase) {
    case SIMREF_DL_ENABLING:
        if (d->step == 3 || d->step == 5 || d->step == 7 || d->step == 9)
            enable_next(d, now);
        else if (d->step == 0)
            enable_next(d, now);
        break;
    case SIMREF_DL_ENABLED:
        if (d->step < 0 && (now >= d->next_cycle_us || d->urgent_zero)) {
            d->urgent_zero = false;
            d->cycle++;
            d->next_cycle_us = (now >= d->next_cycle_us ? d->next_cycle_us : now) + d->cfg.period_us;
            if (d->next_cycle_us <= now)
                d->next_cycle_us = now + d->cfg.period_us;
            d->step = 0;
            cycle_next(d, now);
        }
        break;
    case SIMREF_DL_IDLE:
        if (d->step < 0 && now >= d->next_cycle_us) {
            d->next_cycle_us = now + d->cfg.period_us;
            d->step = 2;
            cycle_next(d, now);
        }
        break;
    default:
        break;
    }
}
