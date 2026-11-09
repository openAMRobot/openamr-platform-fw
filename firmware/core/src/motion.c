/* SPDX-License-Identifier: MIT */
#include "oamr/motion.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

void oamr_motion_default_config(oamr_motion_config_t *cfg)
{
    cfg->cmd_timeout_us = 200000u;
    cfg->host_timeout_us = 500000u;
    cfg->max_linear_m_s = 0.5f;
    cfg->max_wheel_m_s = 0.5f;
    /* No placeholders: these three come only from the commissioning record
     * (oamr/commissioning.h). TODO(Evans): measure and rule them. */
    cfg->accel_limit_m_s2 = NAN;
    cfg->decel_limit_m_s2 = 1.5f;
    cfg->decel_limit_hold_m_s2 = 1.0f;
    cfg->wheel_radius_m = NAN;
    cfg->track_width_m = NAN;
    cfg->max_ramp_dt_s = 0.05f;
    cfg->commissioned = false;
}

void oamr_motion_init(oamr_motion_t *m, const oamr_motion_config_t *cfg)
{
    memset(m, 0, sizeof(*m));
    m->cfg = *cfg;
    m->applied.state = OAMR_APPLIED_INHIBITED;
    m->applied.cmd_age_us = UINT64_MAX;
}

void oamr_motion_cmd_vel(oamr_motion_t *m, float linear_m_s, float angular_rad_s, uint64_t now_us)
{
    if (!isfinite(linear_m_s) || !isfinite(angular_rad_s)) {
        m->counters.cmd_rejected_nonfinite++;
        return;
    }
    m->have_cmd = true;
    m->cmd_v = linear_m_s;
    m->cmd_w = angular_rad_s;
    m->cmd_rx_us = now_us;
    m->counters.cmd_received++;
}

void oamr_motion_host_heartbeat(oamr_motion_t *m, uint64_t now_us)
{
    m->have_hb = true;
    m->hb_rx_us = now_us;
}

static float clampf(float x, float lo, float hi)
{
    return (x < lo) ? lo : ((x > hi) ? hi : x);
}

bool oamr_motion_limit(const oamr_motion_config_t *cfg, float v, float w,
                       float *left_m_s, float *right_m_s)
{
    bool limited = false;
    const float v_lim = clampf(v, -cfg->max_linear_m_s, cfg->max_linear_m_s);
    if (v_lim != v) {
        limited = true;
    }
    const float half_track = 0.5f * cfg->track_width_m;
    float l = v_lim - w * half_track;
    float r = v_lim + w * half_track;
    const float peak = fmaxf(fabsf(l), fabsf(r));
    if (peak > cfg->max_wheel_m_s) {
        const float k = cfg->max_wheel_m_s / peak;
        l *= k;
        r *= k;
        limited = true;
    }
    *left_m_s = l;
    *right_m_s = r;
    return limited;
}

static bool decelerating(float current, float target)
{
    return (current * target < 0.0f) || (fabsf(target) < fabsf(current));
}

/* Move both wheels towards their targets by the same fraction of the remaining
 * step, so the ratio between the wheels (the path curvature) is kept. */
static void ramp(oamr_motion_t *m, float tl, float tr, float dt, bool hold_and_move)
{
    const float decel = hold_and_move ? m->cfg.decel_limit_hold_m_s2 : m->cfg.decel_limit_m_s2;
    const float dl = tl - m->out_left_m_s;
    const float dr = tr - m->out_right_m_s;
    const float lim_l = (decelerating(m->out_left_m_s, tl) ? decel : m->cfg.accel_limit_m_s2) * dt;
    const float lim_r = (decelerating(m->out_right_m_s, tr) ? decel : m->cfg.accel_limit_m_s2) * dt;
    float k = 1.0f;
    if (fabsf(dl) > lim_l) {
        k = fminf(k, lim_l / fabsf(dl));
    }
    if (fabsf(dr) > lim_r) {
        k = fminf(k, lim_r / fabsf(dr));
    }
    m->out_left_m_s += k * dl;
    m->out_right_m_s += k * dr;
}

const oamr_motion_applied_t *oamr_motion_tick(oamr_motion_t *m, uint64_t now_us,
                                              bool motion_allowed, bool hold_and_move,
                                              const oamr_motor_backend_t *backend)
{
    float dt = 0.0f;
    if (m->ticked && now_us > m->last_tick_us) {
        dt = (float)(now_us - m->last_tick_us) * 1e-6f;
        if (dt > m->cfg.max_ramp_dt_s) {
            dt = m->cfg.max_ramp_dt_s;
        }
    }
    m->ticked = true;
    m->last_tick_us = now_us;

    const bool driver_ready = backend->is_ready(backend->ctx);
    const bool host_ok = m->have_hb && (now_us - m->hb_rx_us) < m->cfg.host_timeout_us &&
                         now_us >= m->hb_rx_us;
    const bool ready = motion_allowed && driver_ready;

    /* Entry into an allowed state, or return of the host link: everything
     * received before now is stale and must never move the robot. */
    if ((ready && !m->ready_prev) || (host_ok && !m->host_ok_prev)) {
        if (m->have_cmd) {
            m->counters.cmd_discarded_stale_on_entry++;
        }
        m->have_cmd = false;
        m->accept_after_us = now_us;
    }
    m->ready_prev = ready;
    m->host_ok_prev = host_ok;

    oamr_applied_state_t state;
    float tl = 0.0f;
    float tr = 0.0f;
    bool fresh = false;

    if (m->have_cmd && m->cmd_rx_us <= m->accept_after_us) {
        m->have_cmd = false;
        m->counters.cmd_discarded_stale_on_entry++;
    }

    const bool commissioned = m->cfg.commissioned;
    if (!commissioned) {
        state = OAMR_APPLIED_NOT_COMMISSIONED;
        m->have_cmd = false;
    } else if (!motion_allowed) {
        state = OAMR_APPLIED_INHIBITED;
        m->have_cmd = false;
    } else if (!driver_ready) {
        state = OAMR_APPLIED_DRIVER_NOT_READY;
        m->have_cmd = false;
    } else if (!host_ok) {
        state = OAMR_APPLIED_HOST_LOST;
        m->have_cmd = false;
    } else if (!m->have_cmd) {
        state = OAMR_APPLIED_NO_COMMAND;
    } else if (now_us < m->cmd_rx_us || (now_us - m->cmd_rx_us) >= m->cfg.cmd_timeout_us) {
        state = OAMR_APPLIED_TIMEOUT;
    } else {
        state = OAMR_APPLIED_OK;
        fresh = true;
        m->applied.clamped = oamr_motion_limit(&m->cfg, m->cmd_v, m->cmd_w, &tl, &tr);
    }

    if (fresh) {
        ramp(m, tl, tr, dt, hold_and_move);
    } else {
        m->out_left_m_s = 0.0f;
        m->out_right_m_s = 0.0f;
    }

    /* Final gate, at the single write point. */
    if (!motion_allowed || !commissioned) {
        m->out_left_m_s = 0.0f;
        m->out_right_m_s = 0.0f;
    }

    const float inv_r = commissioned ? 1.0f / m->cfg.wheel_radius_m : 0.0f;
    m->applied.state = state;
    m->applied.left_m_s = m->out_left_m_s;
    m->applied.right_m_s = m->out_right_m_s;
    m->applied.left_rad_s = m->out_left_m_s * inv_r;
    m->applied.right_rad_s = m->out_right_m_s * inv_r;
    m->applied.cmd_age_us = m->have_cmd ? (now_us - m->cmd_rx_us) : UINT64_MAX;

    backend->set_wheel_velocity(backend->ctx, m->applied.left_rad_s, m->applied.right_rad_s);
    m->counters.backend_writes++;
    return &m->applied;
}
