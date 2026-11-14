/* SPDX-License-Identifier: MIT */
#include "oamr/diff_odometry.h"

#include <math.h>
#include <string.h>

#define TWO_PI_F 6.283185307179586f
#define PI_F 3.141592653589793f
#define MAX_DT_S 1.0f /* integration step sanity bound, not a timeout */

static bool finite_pos(float v)
{
    return isfinite(v) && v > 0.0f;
}

bool oamr_odom_config_valid(const oamr_odom_config_t *cfg)
{
    return cfg != NULL && finite_pos(cfg->wheel_radius_m) && finite_pos(cfg->track_width_m) &&
           finite_pos(cfg->counts_per_rev) && finite_pos(cfg->speed_rad_s_per_unit) &&
           (cfg->left_sign == 1 || cfg->left_sign == -1) &&
           (cfg->right_sign == 1 || cfg->right_sign == -1) && cfg->max_input_age_us != 0u &&
           cfg->max_input_age_us <= 0x7FFFFFFFu && cfg->max_count_delta != 0u &&
           cfg->max_count_delta <= 0x7FFFFFFFu;
}

bool oamr_odom_init(oamr_odom_t *o, const oamr_odom_config_t *cfg)
{
    memset(o, 0, sizeof(*o));
    if (!oamr_odom_config_valid(cfg)) {
        return false;
    }
    o->cfg = *cfg;
    o->configured = true;
    return true;
}

void oamr_odom_reset_reference(oamr_odom_t *o)
{
    o->have_counts = false;
    o->have_stamp = false;
}

static float wrap_pi(float a)
{
    a = fmodf(a, TWO_PI_F);
    if (a <= -PI_F) {
        a += TWO_PI_F;
    } else if (a > PI_F) {
        a -= TWO_PI_F;
    }
    return a;
}

static void twist_from_wheels(const oamr_odom_config_t *cfg, float vl, float vr, oamr_twist2d_t *out)
{
    out->v_left_m_s = vl;
    out->v_right_m_s = vr;
    out->v_m_s = 0.5f * (vr + vl);
    out->w_rad_s = (vr - vl) / cfg->track_width_m;
}

oamr_odom_result_t oamr_odom_twist_from_speeds(const oamr_odom_config_t *cfg, float left_units,
                                               float right_units, oamr_twist2d_t *out)
{
    if (!oamr_odom_config_valid(cfg)) {
        return OAMR_ODOM_NOT_CONFIGURED;
    }
    if (!isfinite(left_units) || !isfinite(right_units)) {
        return OAMR_ODOM_NON_FINITE;
    }
    const float k = cfg->speed_rad_s_per_unit * cfg->wheel_radius_m;
    const float vl = (float)cfg->left_sign * left_units * k;
    const float vr = (float)cfg->right_sign * right_units * k;
    if (!isfinite(vl) || !isfinite(vr)) {
        return OAMR_ODOM_NON_FINITE;
    }
    twist_from_wheels(cfg, vl, vr, out);
    return OAMR_ODOM_OK;
}

void oamr_odom_integrate(oamr_pose2d_t *pose, float v_m_s, float w_rad_s, float dt_s)
{
    const float dtheta = w_rad_s * dt_s;
    const float mid = pose->yaw_rad + 0.5f * dtheta;
    pose->x_m += v_m_s * dt_s * cosf(mid);
    pose->y_m += v_m_s * dt_s * sinf(mid);
    pose->yaw_rad = wrap_pi(pose->yaw_rad + dtheta);
}

/* Common order and age checks. */
static oamr_odom_result_t check_time(oamr_odom_t *o, uint32_t sample_us, uint32_t now_us)
{
    const uint32_t age = (uint32_t)(now_us - sample_us);
    if (age > 0x7FFFFFFFu || age >= o->cfg.max_input_age_us) {
        return OAMR_ODOM_STALE;
    }
    if (o->have_stamp) {
        const uint32_t step = (uint32_t)(sample_us - o->last_sample_us);
        if (step == 0u || step > 0x7FFFFFFFu) {
            return OAMR_ODOM_OUT_OF_ORDER;
        }
    }
    return OAMR_ODOM_OK;
}

static oamr_odom_result_t reject(oamr_odom_t *o, oamr_odom_result_t r)
{
    o->rejected++;
    return r;
}

oamr_odom_result_t oamr_odom_update_speeds(oamr_odom_t *o, float left_units, float right_units,
                                           uint32_t sample_us, uint32_t now_us, float dt_s,
                                           oamr_twist2d_t *twist)
{
    if (!o->configured) {
        return OAMR_ODOM_NOT_CONFIGURED;
    }
    if (!isfinite(dt_s) || dt_s <= 0.0f || dt_s > MAX_DT_S) {
        return reject(o, OAMR_ODOM_BAD_DT);
    }
    oamr_odom_result_t r = check_time(o, sample_us, now_us);
    if (r != OAMR_ODOM_OK) {
        return reject(o, r);
    }
    oamr_twist2d_t t;
    r = oamr_odom_twist_from_speeds(&o->cfg, left_units, right_units, &t);
    if (r != OAMR_ODOM_OK) {
        return reject(o, r);
    }
    o->have_stamp = true;
    o->last_sample_us = sample_us;
    oamr_odom_integrate(&o->pose, t.v_m_s, t.w_rad_s, dt_s);
    *twist = t;
    return OAMR_ODOM_OK;
}

oamr_odom_result_t oamr_odom_update_counts(oamr_odom_t *o, int32_t left_counts, int32_t right_counts,
                                           uint32_t sample_us, uint32_t now_us,
                                           oamr_twist2d_t *twist)
{
    if (!o->configured) {
        return OAMR_ODOM_NOT_CONFIGURED;
    }
    oamr_odom_result_t r = check_time(o, sample_us, now_us);
    if (r != OAMR_ODOM_OK) {
        return reject(o, r);
    }
    if (!o->have_counts) {
        o->have_counts = true;
        o->have_stamp = true;
        o->last_left_counts = left_counts;
        o->last_right_counts = right_counts;
        o->last_sample_us = sample_us;
        return OAMR_ODOM_NO_REFERENCE;
    }
    const uint32_t step_us = (uint32_t)(sample_us - o->last_sample_us);
    const float dt_s = (float)step_us * 1e-6f;
    if (dt_s > MAX_DT_S) {
        /* Too long since the reference: re-anchor instead of integrating. */
        o->last_left_counts = left_counts;
        o->last_right_counts = right_counts;
        o->last_sample_us = sample_us;
        return reject(o, OAMR_ODOM_BAD_DT);
    }
    const int32_t dl = oamr_count_delta(left_counts, o->last_left_counts);
    const int32_t dr = oamr_count_delta(right_counts, o->last_right_counts);
    const uint32_t adl = dl < 0 ? (uint32_t)0u - (uint32_t)dl : (uint32_t)dl;
    const uint32_t adr = dr < 0 ? (uint32_t)0u - (uint32_t)dr : (uint32_t)dr;
    if (adl > o->cfg.max_count_delta || adr > o->cfg.max_count_delta) {
        /* Implausible jump: re-anchor, do not move the pose. */
        o->last_left_counts = left_counts;
        o->last_right_counts = right_counts;
        o->last_sample_us = sample_us;
        return reject(o, OAMR_ODOM_JUMP);
    }
    const float m_per_count = TWO_PI_F * o->cfg.wheel_radius_m / o->cfg.counts_per_rev;
    const float sl = (float)o->cfg.left_sign * (float)dl * m_per_count;
    const float sr = (float)o->cfg.right_sign * (float)dr * m_per_count;
    o->last_left_counts = left_counts;
    o->last_right_counts = right_counts;
    o->last_sample_us = sample_us;
    oamr_twist2d_t t;
    twist_from_wheels(&o->cfg, sl / dt_s, sr / dt_s, &t);
    const float ds = 0.5f * (sl + sr);
    const float dtheta = (sr - sl) / o->cfg.track_width_m;
    oamr_odom_integrate(&o->pose, ds, dtheta, 1.0f);
    *twist = t;
    return OAMR_ODOM_OK;
}
