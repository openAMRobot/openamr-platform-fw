/* SPDX-License-Identifier: MIT */
#include <string.h>

#include "oamr/diff_odometry.h"
#include "test_harness.h"

#define PI 3.14159265358979

/* Test geometry only; real values are TODO(Evans). */
static oamr_odom_config_t cfg(void)
{
    oamr_odom_config_t c;
    memset(&c, 0, sizeof(c));
    c.wheel_radius_m = 0.1f;
    c.track_width_m = 0.5f;
    c.counts_per_rev = 4096.0f;
    c.speed_rad_s_per_unit = 0.1f;
    c.left_sign = 1;
    c.right_sign = 1;
    c.max_input_age_us = 100000u;
    c.max_count_delta = 10000u;
    return c;
}

static void config_required(void)
{
    oamr_odom_t o;
    oamr_odom_config_t c = cfg();
    CHECK(oamr_odom_init(&o, &c));
    const float bad_f[] = {0.0f, -1.0f, NAN, INFINITY};
    for (size_t i = 0; i < 4; i++) {
        c = cfg();
        c.wheel_radius_m = bad_f[i];
        CHECK(!oamr_odom_init(&o, &c));
        c = cfg();
        c.track_width_m = bad_f[i];
        CHECK(!oamr_odom_init(&o, &c));
        c = cfg();
        c.counts_per_rev = bad_f[i];
        CHECK(!oamr_odom_init(&o, &c));
        c = cfg();
        c.speed_rad_s_per_unit = bad_f[i];
        CHECK(!oamr_odom_init(&o, &c));
    }
    c = cfg();
    c.left_sign = 0;
    CHECK(!oamr_odom_init(&o, &c));
    c = cfg();
    c.right_sign = 2;
    CHECK(!oamr_odom_init(&o, &c));
    c = cfg();
    c.max_input_age_us = 0u;
    CHECK(!oamr_odom_init(&o, &c));
    c = cfg();
    c.max_count_delta = 0u;
    CHECK(!oamr_odom_init(&o, &c));
    oamr_twist2d_t t;
    CHECK(oamr_odom_update_speeds(&o, 1.0f, 1.0f, 0u, 0u, 0.01f, &t) == OAMR_ODOM_NOT_CONFIGURED);
    CHECK(oamr_odom_twist_from_speeds(&c, 1.0f, 1.0f, &t) == OAMR_ODOM_NOT_CONFIGURED);
}

static void twist_maths(void)
{
    oamr_odom_config_t c = cfg();
    oamr_twist2d_t t;
    /* 10 units * 0.1 rad/s * 0.1 m = 0.1 m/s per wheel */
    CHECK(oamr_odom_twist_from_speeds(&c, 10.0f, 10.0f, &t) == OAMR_ODOM_OK);
    CHECK_NEAR(t.v_m_s, 0.1, 1e-6);
    CHECK_NEAR(t.w_rad_s, 0.0, 1e-6);
    CHECK(oamr_odom_twist_from_speeds(&c, -10.0f, 10.0f, &t) == OAMR_ODOM_OK);
    CHECK_NEAR(t.v_m_s, 0.0, 1e-6);
    CHECK_NEAR(t.w_rad_s, 0.4, 1e-6); /* (0.1 - -0.1) / 0.5 */
    /* mirrored left wheel (typical for a two-motor drive) */
    c.left_sign = -1;
    CHECK(oamr_odom_twist_from_speeds(&c, -10.0f, 10.0f, &t) == OAMR_ODOM_OK);
    CHECK_NEAR(t.v_m_s, 0.1, 1e-6);
    CHECK_NEAR(t.w_rad_s, 0.0, 1e-6);
    CHECK(oamr_odom_twist_from_speeds(&c, NAN, 1.0f, &t) == OAMR_ODOM_NON_FINITE);
    CHECK(oamr_odom_twist_from_speeds(&c, 1.0f, INFINITY, &t) == OAMR_ODOM_NON_FINITE);
}

static void integrate_circle(void)
{
    /* full circle returns to the start; yaw stays in (-pi, pi] */
    oamr_pose2d_t p = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 1000; i++) {
        oamr_odom_integrate(&p, 1.0f, (float)(2.0 * PI), 0.001f);
        CHECK(p.yaw_rad > -(float)PI - 1e-6f && p.yaw_rad <= (float)PI + 1e-6f);
    }
    CHECK_NEAR(p.x_m, 0.0, 1e-4);
    CHECK_NEAR(p.y_m, 0.0, 1e-4);
    /* straight line along +y after 90 deg turn */
    oamr_pose2d_t q = {0.0f, 0.0f, (float)(PI / 2.0)};
    oamr_odom_integrate(&q, 1.0f, 0.0f, 2.0f);
    CHECK_NEAR(q.x_m, 0.0, 1e-5);
    CHECK_NEAR(q.y_m, 2.0, 1e-5);
}

static void speed_updates(void)
{
    oamr_odom_t o;
    oamr_odom_config_t c = cfg();
    CHECK(oamr_odom_init(&o, &c));
    oamr_twist2d_t t;
    uint32_t ts = 1000u;
    for (int i = 0; i < 100; i++) {
        CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, ts, ts + 500u, 0.01f, &t) == OAMR_ODOM_OK);
        ts += 10000u;
    }
    CHECK_NEAR(o.pose.x_m, 0.1, 1e-5);
    /* duplicate sample time */
    memset(&t, 0, sizeof(t));
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, ts - 10000u, ts, 0.01f, &t) == OAMR_ODOM_OUT_OF_ORDER);
    CHECK(t.v_m_s == 0.0f); /* twist output untouched */
    /* older sample */
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, ts - 20000u, ts, 0.01f, &t) == OAMR_ODOM_STALE ||
          oamr_odom_update_speeds(&o, 10.0f, 10.0f, ts - 20000u, ts, 0.01f, &t) == OAMR_ODOM_OUT_OF_ORDER);
    /* stale: age == limit */
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, ts, ts + 100000u, 0.01f, &t) == OAMR_ODOM_STALE);
    /* future-stamped sample */
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, ts + 5u, ts, 0.01f, &t) == OAMR_ODOM_STALE);
    /* bad dt */
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, ts, ts, 0.0f, &t) == OAMR_ODOM_BAD_DT);
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, ts, ts, NAN, &t) == OAMR_ODOM_BAD_DT);
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, ts, ts, 2.0f, &t) == OAMR_ODOM_BAD_DT);
    CHECK(oamr_odom_update_speeds(&o, NAN, 10.0f, ts, ts, 0.01f, &t) == OAMR_ODOM_NON_FINITE);
    CHECK_NEAR(o.pose.x_m, 0.1, 1e-5); /* no rejected update moved the pose */
    CHECK(o.rejected >= 7u);
}

static void count_updates(void)
{
    oamr_odom_t o;
    oamr_odom_config_t c = cfg();
    CHECK(oamr_odom_init(&o, &c));
    oamr_twist2d_t t;
    /* first sample only sets the reference */
    CHECK(oamr_odom_update_counts(&o, 100, 100, 0u, 0u, &t) == OAMR_ODOM_NO_REFERENCE);
    /* one revolution each in 1 s... too long (> 1 s bound is exclusive) -> use 0.5 s */
    CHECK(oamr_odom_update_counts(&o, 100 + 4096, 100 + 4096, 500000u, 500000u, &t) == OAMR_ODOM_OK);
    CHECK_NEAR(o.pose.x_m, 2.0 * PI * 0.1, 1e-4);
    CHECK_NEAR(t.v_m_s, 2.0 * PI * 0.1 / 0.5, 1e-3);
    /* counter wrap: INT32_MAX -> INT32_MIN is +1 count */
    oamr_odom_reset_reference(&o);
    o.pose.x_m = 0.0f;
    CHECK(oamr_odom_update_counts(&o, INT32_MAX - 99, INT32_MAX - 99, 600000u, 600000u, &t) == OAMR_ODOM_NO_REFERENCE);
    CHECK(oamr_odom_update_counts(&o, INT32_MIN + 100, INT32_MIN + 100, 610000u, 610000u, &t) == OAMR_ODOM_OK);
    CHECK_NEAR(o.pose.x_m, 200.0 * 2.0 * PI * 0.1 / 4096.0, 1e-6);
    CHECK(t.v_m_s > 0.0f);
    /* spin in place */
    oamr_odom_reset_reference(&o);
    memset(&o.pose, 0, sizeof(o.pose));
    CHECK(oamr_odom_update_counts(&o, 0, 0, 700000u, 700000u, &t) == OAMR_ODOM_NO_REFERENCE);
    /* each wheel travels pi*track/4 for a 90 deg turn: counts = that / (2 pi r / 4096) */
    const double arc = PI * 0.5 / 4.0;
    const int32_t n = (int32_t)(arc / (2.0 * PI * 0.1 / 4096.0) + 0.5);
    CHECK(oamr_odom_update_counts(&o, -n, n, 800000u, 800000u, &t) == OAMR_ODOM_OK);
    CHECK_NEAR(o.pose.yaw_rad, PI / 2.0, 2e-3);
    CHECK_NEAR(o.pose.x_m, 0.0, 1e-5);
}

static void count_rejections(void)
{
    oamr_odom_t o;
    oamr_odom_config_t c = cfg();
    CHECK(oamr_odom_init(&o, &c));
    oamr_twist2d_t t;
    CHECK(oamr_odom_update_counts(&o, 0, 0, 1000u, 1000u, &t) == OAMR_ODOM_NO_REFERENCE);
    /* jump above limit: rejected and re-anchored, pose unchanged */
    CHECK(oamr_odom_update_counts(&o, 10001, 0, 11000u, 11000u, &t) == OAMR_ODOM_JUMP);
    CHECK(o.pose.x_m == 0.0f && o.pose.yaw_rad == 0.0f);
    CHECK(oamr_odom_update_counts(&o, 10001 + 10, 10, 21000u, 21000u, &t) == OAMR_ODOM_OK);
    CHECK(o.pose.x_m > 0.0f);
    /* negative jump below -limit */
    CHECK(oamr_odom_update_counts(&o, 10011 - 10001, 10, 31000u, 31000u, &t) == OAMR_ODOM_JUMP);
    /* duplicate time */
    CHECK(oamr_odom_update_counts(&o, 0, 0, 31000u, 31000u, &t) == OAMR_ODOM_OUT_OF_ORDER);
    /* stale */
    CHECK(oamr_odom_update_counts(&o, 0, 0, 41000u, 141000u, &t) == OAMR_ODOM_STALE);
    /* gap above 1 s: re-anchor */
    CHECK(oamr_odom_update_counts(&o, 50, 50, 31000u + 1100000u, 31000u + 1100000u, &t) == OAMR_ODOM_BAD_DT);
    const float x = o.pose.x_m;
    CHECK(oamr_odom_update_counts(&o, 60, 60, 31000u + 1110000u, 31000u + 1110000u, &t) == OAMR_ODOM_OK);
    CHECK_NEAR(o.pose.x_m - x, 10.0 * 2.0 * PI * 0.1 / 4096.0, 1e-6);
    /* clock wrap of sample times */
    oamr_odom_reset_reference(&o);
    CHECK(oamr_odom_update_counts(&o, 0, 0, 0xFFFFFF00u, 0xFFFFFF00u, &t) == OAMR_ODOM_NO_REFERENCE);
    CHECK(oamr_odom_update_counts(&o, 10, 10, 0x00002000u, 0x00002000u, &t) == OAMR_ODOM_OK);
}

static void count_delta_helper(void)
{
    CHECK(oamr_count_delta(5, 3) == 2);
    CHECK(oamr_count_delta(3, 5) == -2);
    CHECK(oamr_count_delta(INT32_MIN, INT32_MAX) == 1);
    CHECK(oamr_count_delta(INT32_MAX, INT32_MIN) == -1);
}

int main(void)
{
    RUN(config_required);
    RUN(twist_maths);
    RUN(integrate_circle);
    RUN(speed_updates);
    RUN(count_updates);
    RUN(count_rejections);
    RUN(count_delta_helper);
    TEST_MAIN_END();
}
