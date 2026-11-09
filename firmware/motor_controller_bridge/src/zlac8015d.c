/* SPDX-License-Identifier: MIT */
#include "oamr/zlac8015d.h"

#include <math.h>
#include <string.h>

#define NMT_START 0x01u
#define NMT_PRE_OPERATIONAL 0x80u
#define HB_BASE 0x700u
#define EMCY_BASE 0x080u

#define OD_CONTROLWORD 0x6040u
#define OD_STATUSWORD 0x6041u
#define OD_FAULT 0x603Fu
#define OD_MODE 0x6060u
#define OD_ACCEL 0x6083u
#define OD_DECEL 0x6084u
#define OD_TARGET_VELOCITY 0x60FFu
#define OD_ACTUAL_VELOCITY 0x606Cu
#define OD_PRODUCER_HEARTBEAT 0x1017u
#define OD_COMM_LOSS_PROTECTION 0x2000u
#define OD_SAVE 0x2010u

#define MODE_PROFILE_VELOCITY 3u
#define CW_FAULT_RESET 0x0080u
#define CW_SHUTDOWN 0x0006u
#define CW_SWITCH_ON 0x0007u
#define CW_ENABLE_OPERATION 0x000Fu
#define SW_STATE_MASK 0x006Fu
#define SW_OPERATION_ENABLED 0x0027u
#define SW_FAULT 0x0008u

#define RPM_PER_RAD_S (60.0f / (2.0f * 3.14159265358979f))

void oamr_zlac_default_config(oamr_zlac_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->node_id = 1u;
    cfg->sdo_timeout_us = 50000u;        /* TODO(Evans) */
    cfg->heartbeat_period_ms = 100u;     /* TODO(Evans), see oamr/commissioning.h */
    cfg->heartbeat_misses = 3u;
    cfg->comm_loss_protection_ms = 250u;
    cfg->accel_time_ms = 500u;           /* TODO(Evans): vendor default kept until ruled */
    cfg->decel_time_ms = 500u;           /* TODO(Evans): vendor default kept until ruled */
    cfg->object_2000_size = 2u;          /* TODO(Evans) */
    cfg->object_2010_size = 2u;          /* TODO(Evans) */
    cfg->retry_delay_us = 1000000u;
    cfg->max_sdo_failures = 3u;
    cfg->invert_left = false;            /* TODO(Evans) */
    cfg->invert_right = false;           /* TODO(Evans) */
    cfg->max_rpm = 1000;
}

void oamr_zlac_apply_commissioning(oamr_zlac_config_t *cfg, const oamr_commissioning_record_t *r)
{
    cfg->accel_time_ms = r->zlac_accel_time_ms;
    cfg->decel_time_ms = r->zlac_decel_time_ms;
    cfg->comm_loss_protection_ms = r->zlac_comm_loss_protection_ms;
    cfg->heartbeat_period_ms = r->zlac_heartbeat_period_ms;
    cfg->heartbeat_misses = r->zlac_heartbeat_misses;
}

static void add_entry(oamr_zlac_t *z, uint16_t index, uint8_t sub, uint8_t size, uint32_t value)
{
    if (z->n_entries < OAMR_ZLAC_MAX_CONFIG) {
        oamr_zlac_config_entry_t *e = &z->entries[z->n_entries++];
        e->index = index;
        e->subindex = sub;
        e->size = size;
        e->value = value;
    }
}

static void enter(oamr_zlac_t *z, oamr_zlac_state_t s, uint64_t now_us)
{
    z->state = s;
    z->step = 0;
    z->phase = 0;
    z->cycle_active = false;
    z->fb_parts = 0;
    z->state_since_us = now_us;
}

static void send_nmt(oamr_zlac_t *z, uint8_t cmd)
{
    oamr_can_frame_t f;
    memset(&f, 0, sizeof(f));
    f.id = 0x000u;
    f.dlc = 2;
    f.data[0] = cmd;
    f.data[1] = z->cfg.node_id;
    (void)oamr_can_send(&z->tx, &f);
}

void oamr_zlac_init(oamr_zlac_t *z, const oamr_zlac_config_t *cfg, const oamr_can_tx_t *tx,
                    uint64_t now_us)
{
    memset(z, 0, sizeof(*z));
    z->cfg = *cfg;
    z->tx = *tx;
    oamr_sdo_init(&z->sdo, cfg->node_id, cfg->sdo_timeout_us);
    add_entry(z, OD_MODE, 0, 1, MODE_PROFILE_VELOCITY);
    add_entry(z, OD_ACCEL, 1, 4, cfg->accel_time_ms);
    add_entry(z, OD_ACCEL, 2, 4, cfg->accel_time_ms);
    add_entry(z, OD_DECEL, 1, 4, cfg->decel_time_ms);
    add_entry(z, OD_DECEL, 2, 4, cfg->decel_time_ms);
    add_entry(z, OD_COMM_LOSS_PROTECTION, 0, cfg->object_2000_size, cfg->comm_loss_protection_ms);
    add_entry(z, OD_PRODUCER_HEARTBEAT, 0, 2, cfg->heartbeat_period_ms);
    /* TODO(Evans): quick stop option 605Ah and quick stop deceleration 6085h
     * are not in the community EDS; add them once the V4.2 manual confirms
     * subindex and size. */
    enter(z, OAMR_ZLAC_INIT, now_us);
}

oamr_zlac_fault_t oamr_zlac_decode_fault(uint32_t raw)
{
    oamr_zlac_fault_t f;
    const uint16_t hi = (uint16_t)(raw >> 16);
    const uint16_t lo = (uint16_t)(raw & 0xFFFFu);
    f.raw = raw;
    f.left = (uint16_t)(hi & ~OAMR_ZLAC_FAULT_DRIVE_WIDE_MASK);
    f.right = (uint16_t)(lo & ~OAMR_ZLAC_FAULT_DRIVE_WIDE_MASK);
    f.drive_wide = (uint16_t)((hi | lo) & OAMR_ZLAC_FAULT_DRIVE_WIDE_MASK);
    return f;
}

static uint32_t mask_size(uint32_t v, uint8_t size)
{
    return (size >= 4) ? v : (v & ((1u << (8u * size)) - 1u));
}

/* Issue the SDO transfer for the current state and step. */
static void issue(oamr_zlac_t *z, uint64_t now_us)
{
    oamr_sdo_client_t *s = &z->sdo;
    const oamr_can_tx_t *tx = &z->tx;
    switch (z->state) {
    case OAMR_ZLAC_CONFIGURING: {
        const oamr_zlac_config_entry_t *e = &z->entries[z->step];
        if (z->phase == 1) {
            (void)oamr_sdo_start_write(s, tx, e->index, e->subindex, e->value, e->size, now_us);
        } else {
            (void)oamr_sdo_start_read(s, tx, e->index, e->subindex, now_us);
        }
        break;
    }
    case OAMR_ZLAC_SAVING:
        (void)oamr_sdo_start_write(s, tx, OD_SAVE, 0, 1u, z->cfg.object_2010_size, now_us);
        break;
    case OAMR_ZLAC_ZEROING:
        (void)oamr_sdo_start_write(s, tx, OD_TARGET_VELOCITY, (uint8_t)(z->step + 1u), 0u, 4, now_us);
        break;
    case OAMR_ZLAC_ENABLING: {
        static const uint16_t cw[4] = {CW_FAULT_RESET, CW_SHUTDOWN, CW_SWITCH_ON, CW_ENABLE_OPERATION};
        if (z->step == 0 && !z->fault_reset_requested) {
            z->step = 1;
        }
        if (z->step < 4) {
            (void)oamr_sdo_start_write(s, tx, OD_CONTROLWORD, 0, cw[z->step], 2, now_us);
        } else {
            (void)oamr_sdo_start_read(s, tx, OD_STATUSWORD, 0, now_us);
        }
        break;
    }
    case OAMR_ZLAC_OPERATIONAL:
    case OAMR_ZLAC_FAULT: {
        const bool op = (z->state == OAMR_ZLAC_OPERATIONAL);
        switch (z->step) {
        case 0:
            (void)oamr_sdo_start_write(s, tx, OD_TARGET_VELOCITY, 1,
                                       (uint32_t)(op ? z->target_rpm_left : 0), 4, now_us);
            break;
        case 1:
            (void)oamr_sdo_start_write(s, tx, OD_TARGET_VELOCITY, 2,
                                       (uint32_t)(op ? z->target_rpm_right : 0), 4, now_us);
            break;
        case 2: (void)oamr_sdo_start_read(s, tx, OD_ACTUAL_VELOCITY, 1, now_us); break;
        case 3: (void)oamr_sdo_start_read(s, tx, OD_ACTUAL_VELOCITY, 2, now_us); break;
        case 4: (void)oamr_sdo_start_read(s, tx, OD_FAULT, 0, now_us); break;
        default: (void)oamr_sdo_start_read(s, tx, OD_STATUSWORD, 0, now_us); break;
        }
        break;
    }
    default:
        break;
    }
}

static void config_failed(oamr_zlac_t *z, uint64_t now_us)
{
    const oamr_zlac_config_entry_t *e = &z->entries[z->step];
    z->failed_index = e->index;
    z->failed_subindex = e->subindex;
    z->failed_abort_code = z->sdo.abort_code;
    enter(z, OAMR_ZLAC_CONFIG_FAILED, now_us);
}

static void start_heartbeat_watch(oamr_zlac_t *z, uint64_t now_us)
{
    z->hb_watch = true;
    z->hb_watch_from_us = now_us;
}

static float to_rad_s(int32_t v_0p1rpm, bool invert)
{
    const float rad_s = (float)v_0p1rpm * 0.1f / RPM_PER_RAD_S;
    return invert ? -rad_s : rad_s;
}

static void complete(oamr_zlac_t *z, uint64_t now_us)
{
    const oamr_sdo_state_t res = z->sdo.state;
    const uint32_t value = z->sdo.value;
    if (res != OAMR_SDO_DONE) {
        z->sdo_failures_total++;
        z->consecutive_sdo_failures++;
        if (z->state == OAMR_ZLAC_CONFIGURING && res == OAMR_SDO_ABORTED) {
            config_failed(z, now_us);
            oamr_sdo_release(&z->sdo);
            return;
        }
        oamr_sdo_release(&z->sdo);
        if (z->consecutive_sdo_failures >= z->cfg.max_sdo_failures) {
            z->hb_lost_waiting = false;
            z->hb_watch = false;
            enter(z, OAMR_ZLAC_COMM_LOST, now_us);
            return;
        }
        issue(z, now_us); /* retry the same transfer */
        return;
    }
    oamr_sdo_release(&z->sdo);
    z->consecutive_sdo_failures = 0;

    switch (z->state) {
    case OAMR_ZLAC_CONFIGURING: {
        const oamr_zlac_config_entry_t *e = &z->entries[z->step];
        const bool equal = mask_size(value, e->size) == mask_size(e->value, e->size);
        if (z->phase == 0 && !equal) {
            z->phase = 1;
            issue(z, now_us);
            return;
        }
        if (z->phase == 1) {
            z->config_writes++;
            z->config_changed = true;
            z->phase = 2;
            issue(z, now_us);
            return;
        }
        if (z->phase == 2 && !equal) {
            config_failed(z, now_us);
            return;
        }
        z->step++;
        z->phase = 0;
        if (z->step >= z->n_entries) {
            enter(z, z->config_changed ? OAMR_ZLAC_SAVING : OAMR_ZLAC_ZEROING, now_us);
        }
        issue(z, now_us);
        return;
    }
    case OAMR_ZLAC_SAVING:
        z->saves++;
        enter(z, OAMR_ZLAC_ZEROING, now_us);
        issue(z, now_us);
        return;
    case OAMR_ZLAC_ZEROING:
        z->step++;
        if (z->step >= 2) {
            send_nmt(z, NMT_START);
            start_heartbeat_watch(z, now_us);
            enter(z, OAMR_ZLAC_ENABLING, now_us);
        }
        issue(z, now_us);
        return;
    case OAMR_ZLAC_ENABLING:
        if (z->step < 4) {
            z->step++;
            issue(z, now_us);
            return;
        }
        z->statusword = (uint16_t)value;
        z->fault_reset_requested = false;
        if ((z->statusword & SW_STATE_MASK) == SW_OPERATION_ENABLED) {
            enter(z, OAMR_ZLAC_OPERATIONAL, now_us);
        } else {
            enter(z, OAMR_ZLAC_FAULT, now_us); /* enable refused: latched */
        }
        return;
    case OAMR_ZLAC_OPERATIONAL:
    case OAMR_ZLAC_FAULT:
        switch (z->step) {
        case 0:
        case 1:
            break; /* target velocity write acknowledged */
        case 2:
            z->fb_left_0p1rpm = (int32_t)value;
            z->fb_parts |= 1u;
            break;
        case 3:
            z->fb_right_0p1rpm = (int32_t)value;
            z->fb_parts |= 2u;
            if (z->fb_parts == 3u) {
                z->feedback.valid = true;
                z->feedback.stamp_us = now_us;
                z->feedback.left_rad_s = to_rad_s(z->fb_left_0p1rpm, z->cfg.invert_left);
                z->feedback.right_rad_s = to_rad_s(z->fb_right_0p1rpm, z->cfg.invert_right);
            }
            break;
        case 4:
            z->fault = oamr_zlac_decode_fault(value);
            if (z->fault.raw != 0u && z->state == OAMR_ZLAC_OPERATIONAL) {
                enter(z, OAMR_ZLAC_FAULT, now_us);
                return;
            }
            break;
        default: /* step 5: statusword */
            z->statusword = (uint16_t)value;
            if (z->state == OAMR_ZLAC_OPERATIONAL &&
                (z->statusword & SW_STATE_MASK) != SW_OPERATION_ENABLED) {
                enter(z, OAMR_ZLAC_FAULT, now_us); /* fault or left Operation Enabled */
                return;
            }
            break;
        }
        z->step++;
        if (z->step >= 6) {
            z->cycle_active = false;
            z->fb_parts = 0;
            return;
        }
        issue(z, now_us);
        return;
    default:
        return;
    }
}

void oamr_zlac_on_frame(oamr_zlac_t *z, const oamr_can_frame_t *f, uint64_t now_us)
{
    if (f->extended) {
        return;
    }
    if (f->id == HB_BASE + z->cfg.node_id && f->dlc >= 1) {
        z->hb_seen = true;
        z->hb_rx_us = now_us;
        z->nmt_state = f->data[0];
        z->hb_lost_waiting = false;
        if (f->data[0] == 0x00u && z->state != OAMR_ZLAC_INIT) {
            /* Boot-up: the drive restarted and lost its runtime state. */
            z->sdo.state = OAMR_SDO_IDLE;
            z->hb_watch = false;
            enter(z, OAMR_ZLAC_INIT, now_us);
        }
        return;
    }
    if (f->id == EMCY_BASE + z->cfg.node_id && f->dlc >= 2) {
        z->last_emcy_code = (uint16_t)(f->data[0] | ((uint16_t)f->data[1] << 8));
        return;
    }
    if (oamr_sdo_on_frame(&z->sdo, f) && oamr_sdo_finished(&z->sdo)) {
        complete(z, now_us);
    }
}

static bool heartbeat_fresh(const oamr_zlac_t *z, uint64_t now_us)
{
    const uint64_t limit = (uint64_t)z->cfg.heartbeat_misses * z->cfg.heartbeat_period_ms * 1000u;
    uint64_t ref = z->hb_watch_from_us;
    if (z->hb_seen && z->hb_rx_us > ref) {
        ref = z->hb_rx_us;
    }
    return now_us >= ref && (now_us - ref) < limit;
}

void oamr_zlac_tick(oamr_zlac_t *z, uint64_t now_us)
{
    oamr_sdo_poll(&z->sdo, &z->tx, now_us);
    if (oamr_sdo_finished(&z->sdo)) {
        complete(z, now_us);
    }

    if (z->hb_watch && !heartbeat_fresh(z, now_us)) {
        z->heartbeat_losses++;
        z->hb_watch = false;
        z->hb_lost_waiting = true;
        z->sdo.state = OAMR_SDO_IDLE;
        z->feedback.valid = false;
        enter(z, OAMR_ZLAC_COMM_LOST, now_us);
        return;
    }

    switch (z->state) {
    case OAMR_ZLAC_INIT:
        z->boot_attempts++;
        z->config_changed = false;
        z->config_writes = 0;
        z->consecutive_sdo_failures = 0;
        z->feedback.valid = false;
        z->hb_watch = false;
        send_nmt(z, NMT_PRE_OPERATIONAL);
        enter(z, OAMR_ZLAC_CONFIGURING, now_us);
        issue(z, now_us);
        break;
    case OAMR_ZLAC_COMM_LOST:
        z->feedback.valid = false;
        if (!z->hb_lost_waiting && now_us - z->state_since_us >= z->cfg.retry_delay_us) {
            enter(z, OAMR_ZLAC_INIT, now_us);
        }
        break;
    case OAMR_ZLAC_FAULT:
        if (z->fault_reset_requested && z->sdo.state == OAMR_SDO_IDLE && !z->cycle_active) {
            enter(z, OAMR_ZLAC_ENABLING, now_us);
            issue(z, now_us);
            break;
        }
        /* Keep reading the fault and writing zero targets. */
        /* fall through */
    case OAMR_ZLAC_OPERATIONAL:
        if (z->sdo.state == OAMR_SDO_IDLE) {
            z->step = 0;
            z->fb_parts = 0;
            z->cycle_active = true;
            issue(z, now_us);
        }
        break;
    case OAMR_ZLAC_CONFIG_FAILED:
        break;
    default:
        /* A transfer that could not be queued is retried here. */
        if (z->sdo.state == OAMR_SDO_IDLE) {
            issue(z, now_us);
        }
        break;
    }
}

void oamr_zlac_set_velocity(oamr_zlac_t *z, float left_rad_s, float right_rad_s)
{
    float l = left_rad_s * RPM_PER_RAD_S;
    float r = right_rad_s * RPM_PER_RAD_S;
    if (!isfinite(l) || !isfinite(r)) {
        l = 0.0f;
        r = 0.0f;
    }
    const float max = (float)z->cfg.max_rpm;
    l = fminf(fmaxf(l, -max), max);
    r = fminf(fmaxf(r, -max), max);
    z->target_rpm_left = (int32_t)lroundf(z->cfg.invert_left ? -l : l);
    z->target_rpm_right = (int32_t)lroundf(z->cfg.invert_right ? -r : r);
}

bool oamr_zlac_ready(const oamr_zlac_t *z, uint64_t now_us)
{
    return z->state == OAMR_ZLAC_OPERATIONAL && z->hb_watch && heartbeat_fresh(z, now_us) &&
           z->feedback.valid && z->fault.raw == 0u;
}

void oamr_zlac_request_fault_reset(oamr_zlac_t *z)
{
    if (z->state == OAMR_ZLAC_FAULT) {
        z->fault_reset_requested = true;
    }
}

void oamr_zlac_request_reinit(oamr_zlac_t *z, uint64_t now_us)
{
    if (z->state == OAMR_ZLAC_CONFIG_FAILED) {
        enter(z, OAMR_ZLAC_INIT, now_us);
    }
}

static void backend_set(void *ctx, float l, float r)
{
    oamr_zlac_set_velocity(((oamr_zlac_backend_ctx_t *)ctx)->zlac, l, r);
}

static bool backend_ready(void *ctx)
{
    const oamr_zlac_backend_ctx_t *c = (const oamr_zlac_backend_ctx_t *)ctx;
    return oamr_zlac_ready(c->zlac, c->now_us(c->clock_ctx));
}

static oamr_wheel_feedback_t backend_feedback(void *ctx)
{
    return ((oamr_zlac_backend_ctx_t *)ctx)->zlac->feedback;
}

oamr_motor_backend_t oamr_zlac_backend(oamr_zlac_backend_ctx_t *ctx)
{
    oamr_motor_backend_t b;
    b.set_wheel_velocity = backend_set;
    b.is_ready = backend_ready;
    b.feedback = backend_feedback;
    b.ctx = ctx;
    return b;
}
