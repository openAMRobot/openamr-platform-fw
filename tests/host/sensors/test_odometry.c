/* SPDX-License-Identifier: MIT */
#include <string.h>

#include "oamr/diff_odometry.h"
#include "test_harness.h"

#define PI 3.14159265358979

/* Golden values in this file: the geometry below is a test input, not the
 * robot's (real values are TODO(Evans)). Every expected pose and twist is
 * hand arithmetic from v = (vr + vl) / 2, w = (vr - vl) / track and the
 * midpoint rule stated in diff_odometry.h (no external source). */

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
    c.max_gap_us = 100000u;
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
    c.max_gap_us = 0u;
    CHECK(!oamr_odom_init(&o, &c));
    c = cfg();
    c.max_count_delta = 0u;
    CHECK(!oamr_odom_init(&o, &c));
    oamr_twist2d_t t;
    CHECK(oamr_odom_update_speeds(&o, 1.0f, 1.0f, 0u, 0u, &t) == OAMR_ODOM_NOT_CONFIGURED);
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

/* 10 units * 0.1 rad/s per unit * 0.1 m = 0.1 m/s on each wheel. */
#define V_STRAIGHT 0.1

static void speed_20ms_spacing(void)
{
    oamr_odom_t o;
    oamr_odom_config_t c = cfg();
    CHECK(oamr_odom_init(&o, &c));
    oamr_twist2d_t t;
    uint64_t ts = 1000u;
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, ts, ts + 500u, &t) == OAMR_ODOM_NO_REFERENCE);
    CHECK(o.pose.x_m == 0.0f);
    for (int i = 0; i < 50; i++) {
        ts += 20000u;
        CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, ts, ts + 500u, &t) == OAMR_ODOM_OK);
    }
    /* 50 intervals of 20 ms at 0.1 m/s = 0.1 m */
    CHECK_NEAR(o.pose.x_m, 50 * 0.020 * V_STRAIGHT, 1e-5);
    CHECK_NEAR(t.v_m_s, V_STRAIGHT, 1e-6);
    CHECK(o.rejected == 0u && !o.continuity_lost);
}

static void speed_late_sample(void)
{
    /* A sample that arrives 35 ms after the previous one (instead of 20 ms)
     * is integrated over the 35 ms its stamps say, not over a fixed step. */
    oamr_odom_t o;
    oamr_odom_config_t c = cfg();
    CHECK(oamr_odom_init(&o, &c));
    oamr_twist2d_t t;
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, 0u, 0u, &t) == OAMR_ODOM_NO_REFERENCE);
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, 20000u, 20000u, &t) == OAMR_ODOM_OK);
    CHECK_NEAR(o.pose.x_m, 0.020 * V_STRAIGHT, 1e-7);
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, 55000u, 56000u, &t) == OAMR_ODOM_OK);
    CHECK_NEAR(o.pose.x_m, 0.055 * V_STRAIGHT, 1e-7);
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, 75000u, 75000u, &t) == OAMR_ODOM_OK);
    CHECK_NEAR(o.pose.x_m, 0.075 * V_STRAIGHT, 1e-7);
    CHECK(o.rejected == 0u && !o.continuity_lost);
}

static void speed_repeated_stamp(void)
{
    oamr_odom_t o;
    oamr_odom_config_t c = cfg();
    CHECK(oamr_odom_init(&o, &c));
    oamr_twist2d_t t;
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, 0u, 0u, &t) == OAMR_ODOM_NO_REFERENCE);
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, 20000u, 20000u, &t) == OAMR_ODOM_OK);
    const float x = o.pose.x_m;
    memset(&t, 0, sizeof(t));
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, 20000u, 21000u, &t) == OAMR_ODOM_OUT_OF_ORDER);
    CHECK(t.v_m_s == 0.0f); /* twist output untouched */
    CHECK(o.pose.x_m == x);
    CHECK(o.rejected == 1u && !o.continuity_lost);
    /* the next sample integrates from the last accepted stamp */
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, 40000u, 40000u, &t) == OAMR_ODOM_OK);
    CHECK_NEAR(o.pose.x_m, 0.040 * V_STRAIGHT, 1e-7);
}

static void speed_stamp_backwards(void)
{
    oamr_odom_t o;
    oamr_odom_config_t c = cfg();
    CHECK(oamr_odom_init(&o, &c));
    oamr_twist2d_t t;
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, 40000u, 40000u, &t) == OAMR_ODOM_NO_REFERENCE);
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, 60000u, 60000u, &t) == OAMR_ODOM_OK);
    const float x = o.pose.x_m;
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, 50000u, 60000u, &t) == OAMR_ODOM_OUT_OF_ORDER);
    CHECK(oamr_odom_update_speeds(&o, -50.0f, 10.0f, 1u, 60000u, &t) == OAMR_ODOM_OUT_OF_ORDER);
    CHECK(o.pose.x_m == x && o.pose.yaw_rad == 0.0f);
    CHECK(o.rejected == 2u);
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, 80000u, 80000u, &t) == OAMR_ODOM_OK);
    CHECK_NEAR(o.pose.x_m, 0.040 * V_STRAIGHT, 1e-7);
}

static void speed_other_rejections(void)
{
    oamr_odom_t o;
    oamr_odom_config_t c = cfg();
    CHECK(oamr_odom_init(&o, &c));
    oamr_twist2d_t t;
    const uint64_t ts = 1000000u;
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, ts, ts, &t) == OAMR_ODOM_NO_REFERENCE);
    /* stale: age == limit */
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, ts + 1u, ts + 1u + 100000u, &t) == OAMR_ODOM_STALE);
    /* future-stamped sample */
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, ts + 5u, ts, &t) == OAMR_ODOM_STALE);
    CHECK(oamr_odom_update_speeds(&o, NAN, 10.0f, ts + 10u, ts + 10u, &t) == OAMR_ODOM_NON_FINITE);
    CHECK(o.pose.x_m == 0.0f);
    CHECK(o.rejected == 3u && !o.continuity_lost);
    /* stamps above 2^32 us work like any other */
    oamr_odom_reset_reference(&o);
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, 0xFFFFC000ull, 0xFFFFC000ull, &t) ==
          OAMR_ODOM_NO_REFERENCE);
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, 0x100000E20ull, 0x100000E20ull, &t) ==
          OAMR_ODOM_OK); /* 0x4E20 us = 20 ms later */
    CHECK_NEAR(o.pose.x_m, 0.020 * V_STRAIGHT, 1e-7);
}

static void speed_gap_then_resumed_motion(void)
{
    oamr_odom_t o;
    oamr_odom_config_t c = cfg(); /* max_gap_us 100 ms */
    CHECK(oamr_odom_init(&o, &c));
    oamr_twist2d_t t;
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, 0u, 0u, &t) == OAMR_ODOM_NO_REFERENCE);
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, 20000u, 20000u, &t) == OAMR_ODOM_OK);
    /* an interval of exactly the limit is still integrated */
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, 120000u, 120000u, &t) == OAMR_ODOM_OK);
    CHECK_NEAR(o.pose.x_m, 0.120 * V_STRAIGHT, 1e-7);
    CHECK(!o.continuity_lost && o.continuity_lost_events == 0u);
    /* gap: 500 ms without a sample */
    const float x = o.pose.x_m;
    memset(&t, 0, sizeof(t));
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, 620000u, 620000u, &t) == OAMR_ODOM_GAP);
    CHECK(o.pose.x_m == x); /* nothing integrated across the gap */
    CHECK(t.v_m_s == 0.0f);
    CHECK(o.continuity_lost && o.continuity_lost_events == 1u);
    /* resumed motion integrates from the sample after the gap ... */
    for (int i = 1; i <= 5; i++) {
        const uint64_t ts = 620000u + (uint64_t)i * 20000u;
        CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, ts, ts, &t) == OAMR_ODOM_OK);
    }
    CHECK_NEAR(o.pose.x_m - x, 5 * 0.020 * V_STRAIGHT, 1e-6);
    /* ... and the flag stays set until the caller clears it */
    CHECK(o.continuity_lost);
    oamr_odom_clear_continuity(&o);
    CHECK(!o.continuity_lost && o.continuity_lost_events == 1u);
    /* a second gap counts again */
    CHECK(oamr_odom_update_speeds(&o, 10.0f, 10.0f, 2000000u, 2000000u, &t) == OAMR_ODOM_GAP);
    CHECK(o.continuity_lost && o.continuity_lost_events == 2u);
}

static void count_updates(void)
{
    oamr_odom_t o;
    oamr_odom_config_t c = cfg();
    CHECK(oamr_odom_init(&o, &c));
    oamr_twist2d_t t;
    /* first sample only sets the reference */
    CHECK(oamr_odom_update_counts(&o, 100, 100, 0u, 0u, &t) == OAMR_ODOM_NO_REFERENCE);
    /* one revolution each in 100 ms (the gap limit of the test config) */
    CHECK(oamr_odom_update_counts(&o, 100 + 4096, 100 + 4096, 100000u, 100000u, &t) == OAMR_ODOM_OK);
    CHECK_NEAR(o.pose.x_m, 2.0 * PI * 0.1, 1e-4);
    CHECK_NEAR(t.v_m_s, 2.0 * PI * 0.1 / 0.1, 1e-3);
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
    /* jump above limit: continuity lost, re-referenced, pose unchanged */
    CHECK(oamr_odom_update_counts(&o, 10001, 0, 11000u, 11000u, &t) == OAMR_ODOM_JUMP);
    CHECK(o.pose.x_m == 0.0f && o.pose.yaw_rad == 0.0f);
    CHECK(o.continuity_lost && o.continuity_lost_events == 1u);
    CHECK(oamr_odom_update_counts(&o, 10001 + 10, 10, 21000u, 21000u, &t) == OAMR_ODOM_OK);
    CHECK(o.pose.x_m > 0.0f);
    CHECK(o.continuity_lost); /* sticky */
    oamr_odom_clear_continuity(&o);
    /* negative jump below -limit */
    CHECK(oamr_odom_update_counts(&o, 10011 - 10001, 10, 31000u, 31000u, &t) == OAMR_ODOM_JUMP);
    CHECK(o.continuity_lost_events == 2u);
    /* repeated stamp */
    CHECK(oamr_odom_update_counts(&o, 0, 0, 31000u, 31000u, &t) == OAMR_ODOM_OUT_OF_ORDER);
    /* stamp going backwards */
    CHECK(oamr_odom_update_counts(&o, 0, 0, 30000u, 31000u, &t) == OAMR_ODOM_OUT_OF_ORDER);
    /* stale */
    CHECK(oamr_odom_update_counts(&o, 0, 0, 41000u, 141000u, &t) == OAMR_ODOM_STALE);
    CHECK(o.continuity_lost_events == 2u); /* rejections are not continuity events */
}

static void count_gap_then_resumed_motion(void)
{
    oamr_odom_t o;
    oamr_odom_config_t c = cfg();
    CHECK(oamr_odom_init(&o, &c));
    oamr_twist2d_t t;
    CHECK(oamr_odom_update_counts(&o, 0, 0, 0u, 0u, &t) == OAMR_ODOM_NO_REFERENCE);
    CHECK(oamr_odom_update_counts(&o, 10, 10, 20000u, 20000u, &t) == OAMR_ODOM_OK);
    const float x0 = o.pose.x_m;
    /* 1.1 s gap; the wheels moved 50 counts in it, which cannot be placed */
    CHECK(oamr_odom_update_counts(&o, 60, 60, 1120000u, 1120000u, &t) == OAMR_ODOM_GAP);
    CHECK(o.pose.x_m == x0);
    CHECK(o.continuity_lost && o.continuity_lost_events == 1u);
    /* resumed motion: 10 counts per 20 ms from the post-gap reference */
    CHECK(oamr_odom_update_counts(&o, 70, 70, 1140000u, 1140000u, &t) == OAMR_ODOM_OK);
    CHECK(oamr_odom_update_counts(&o, 80, 80, 1160000u, 1160000u, &t) == OAMR_ODOM_OK);
    CHECK_NEAR(o.pose.x_m - x0, 20.0 * 2.0 * PI * 0.1 / 4096.0, 1e-6);
    CHECK_NEAR(t.v_m_s, 10.0 * 2.0 * PI * 0.1 / 4096.0 / 0.020, 1e-5);
    CHECK(o.continuity_lost);
    oamr_odom_clear_continuity(&o);
    CHECK(!o.continuity_lost && o.continuity_lost_events == 1u);
    /* the caller's own reference reset is not a continuity event */
    oamr_odom_reset_reference(&o);
    CHECK(oamr_odom_update_counts(&o, 0, 0, 5000000u, 5000000u, &t) == OAMR_ODOM_NO_REFERENCE);
    CHECK(!o.continuity_lost && o.continuity_lost_events == 1u);
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
    RUN(speed_20ms_spacing);
    RUN(speed_late_sample);
    RUN(speed_repeated_stamp);
    RUN(speed_stamp_backwards);
    RUN(speed_other_rejections);
    RUN(speed_gap_then_resumed_motion);
    RUN(count_updates);
    RUN(count_rejections);
    RUN(count_gap_then_resumed_motion);
    RUN(count_delta_helper);
    TEST_MAIN_END();
}
