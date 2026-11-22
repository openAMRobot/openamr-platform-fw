/* SPDX-License-Identifier: MIT */
#include "oamr/commissioning.h"

#include <math.h>
#include <string.h>

void oamr_commissioning_record_init(oamr_commissioning_record_t *r)
{
    memset(r, 0, sizeof(*r));
    r->wheel_radius_m = NAN;
    r->track_width_m = NAN;
    r->accel_limit_m_s2 = NAN;
}

static bool positive(float x)
{
    return isfinite(x) && x > 0.0f;
}

uint32_t oamr_commissioning_missing(const oamr_commissioning_record_t *r)
{
    uint32_t m = 0u;
    if (!positive(r->wheel_radius_m)) {
        m |= OAMR_COMM_WHEEL_RADIUS;
    }
    if (!positive(r->track_width_m)) {
        m |= OAMR_COMM_TRACK_WIDTH;
    }
    if (!positive(r->accel_limit_m_s2)) {
        m |= OAMR_COMM_ACCEL_LIMIT;
    }
    if (r->zlac_accel_time_ms == 0u || r->zlac_decel_time_ms == 0u) {
        m |= OAMR_COMM_ZLAC_RAMPS;
    }
    if (r->zlac_comm_loss_protection_ms < OAMR_COMM_LINK_LOSS_MIN_MS ||
        r->zlac_comm_loss_protection_ms > OAMR_COMM_LINK_LOSS_MAX_MS ||
        r->zlac_heartbeat_period_ms == 0u ||
        r->zlac_heartbeat_misses < OAMR_COMM_MIN_HEARTBEAT_MISSES) {
        m |= OAMR_COMM_ZLAC_LINK_LOSS;
    }
    return m;
}

bool oamr_commissioning_complete(const oamr_commissioning_record_t *r)
{
    return oamr_commissioning_missing(r) == 0u;
}

uint32_t oamr_commissioning_consumer_timeout_ms(const oamr_commissioning_record_t *r)
{
    return (uint32_t)r->zlac_heartbeat_misses * r->zlac_heartbeat_period_ms;
}

void oamr_commissioning_apply_motion(const oamr_commissioning_record_t *r,
                                     oamr_motion_config_t *cfg)
{
    const uint32_t need = OAMR_COMM_WHEEL_RADIUS | OAMR_COMM_TRACK_WIDTH | OAMR_COMM_ACCEL_LIMIT;
    if ((oamr_commissioning_missing(r) & need) != 0u) {
        cfg->commissioned = false;
        return;
    }
    cfg->wheel_radius_m = r->wheel_radius_m;
    cfg->track_width_m = r->track_width_m;
    cfg->accel_limit_m_s2 = r->accel_limit_m_s2;
    cfg->commissioned = true;
}

bool oamr_commissioning_sw_stop(const oamr_commissioning_record_t *r, bool host_sw_stop)
{
    return host_sw_stop || !oamr_commissioning_complete(r);
}
