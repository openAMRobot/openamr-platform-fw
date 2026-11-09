/* SPDX-License-Identifier: MIT */
#include "oamr/odometry.h"

#include <math.h>
#include <string.h>

#define OAMR_PI 3.14159265358979323846

void oamr_odom_init(oamr_odom_t *o)
{
    memset(o, 0, sizeof(*o));
}

static double wrap(double a)
{
    while (a > OAMR_PI) {
        a -= 2.0 * OAMR_PI;
    }
    while (a <= -OAMR_PI) {
        a += 2.0 * OAMR_PI;
    }
    return a;
}

bool oamr_odom_update(oamr_odom_t *o, float left_rad_s, float right_rad_s, uint64_t stamp_us,
                      float wheel_radius_m, float track_width_m, uint64_t max_gap_us)
{
    if (!isfinite(wheel_radius_m) || !isfinite(track_width_m) || wheel_radius_m <= 0.0f ||
        track_width_m <= 0.0f || !isfinite(left_rad_s) || !isfinite(right_rad_s)) {
        return false;
    }
    const float vl = left_rad_s * wheel_radius_m;
    const float vr = right_rad_s * wheel_radius_m;
    const float v = 0.5f * (vl + vr);
    const float w = (vr - vl) / track_width_m;

    if (!o->valid) {
        o->valid = true;
        o->stamp_us = stamp_us;
        o->linear_m_s = v;
        o->angular_rad_s = w;
        return true;
    }
    if (stamp_us <= o->stamp_us) {
        return false;
    }
    const uint64_t gap = stamp_us - o->stamp_us;
    o->stamp_us = stamp_us;
    if (gap > max_gap_us) {
        o->gaps++;
        o->linear_m_s = v;
        o->angular_rad_s = w;
        return false;
    }
    const double dt = (double)gap * 1e-6;
    const double mid_yaw = o->yaw_rad + 0.5 * (double)w * dt;
    o->x_m += (double)v * dt * cos(mid_yaw);
    o->y_m += (double)v * dt * sin(mid_yaw);
    o->yaw_rad = wrap(o->yaw_rad + (double)w * dt);
    o->linear_m_s = v;
    o->angular_rad_s = w;
    return true;
}
