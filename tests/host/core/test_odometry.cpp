// SPDX-License-Identifier: MIT
// Differential-drive odometry: straight line, spin in place, arc, gaps,
// refusal without geometry.
#include <gtest/gtest.h>

#include <cmath>

#include "test_params.h"

extern "C" {
#include "oamr/odometry.h"
}

namespace {

constexpr uint64_t MS = 1000;
constexpr float R = kTestWheelRadiusM;
constexpr float W = kTestTrackWidthM;

void drive(oamr_odom_t *o, float l, float r, uint64_t from, uint64_t to, uint64_t dt = 20 * MS)
{
    for (uint64_t t = from; t <= to; t += dt) {
        oamr_odom_update(o, l, r, t, R, W, 500 * MS);
    }
}

TEST(Odometry, FirstSampleOnlyAnchorsTime)
{
    oamr_odom_t o;
    oamr_odom_init(&o);
    EXPECT_TRUE(oamr_odom_update(&o, 5.0f, 5.0f, 1000 * MS, R, W, 500 * MS));
    EXPECT_TRUE(o.valid);
    EXPECT_DOUBLE_EQ(o.x_m, 0.0);
    EXPECT_NEAR(o.linear_m_s, 0.5f, 1e-6);
}

TEST(Odometry, StraightLine)
{
    oamr_odom_t o;
    oamr_odom_init(&o);
    drive(&o, 5.0f, 5.0f, 0, 2000 * MS);  // 0.5 m/s for 2 s
    EXPECT_NEAR(o.x_m, 1.0, 1e-6);
    EXPECT_NEAR(o.y_m, 0.0, 1e-9);
    EXPECT_NEAR(o.yaw_rad, 0.0, 1e-9);
    EXPECT_NEAR(o.angular_rad_s, 0.0f, 1e-6);
}

TEST(Odometry, SpinInPlaceWrapsYaw)
{
    oamr_odom_t o;
    oamr_odom_init(&o);
    // vr - vl = 2 * 0.2 m/s -> w = 0.4 / 0.4 = 1 rad/s
    drive(&o, -2.0f, 2.0f, 0, 4000 * MS);
    EXPECT_NEAR(o.x_m, 0.0, 1e-9);
    EXPECT_NEAR(o.angular_rad_s, 1.0f, 1e-6);
    EXPECT_NEAR(o.yaw_rad, 4.0 - 2.0 * M_PI, 1e-6);
    EXPECT_LE(o.yaw_rad, M_PI);
    EXPECT_GT(o.yaw_rad, -M_PI);
}

TEST(Odometry, QuarterCircleArc)
{
    oamr_odom_t o;
    oamr_odom_init(&o);
    // v = 0.5 m/s, w = 0.5 rad/s -> radius 1 m. vl = 0.4, vr = 0.6.
    const double t_quarter = (M_PI / 2.0) / 0.5;
    const uint64_t end = static_cast<uint64_t>(t_quarter * 1e6);
    drive(&o, 4.0f, 6.0f, 0, end, 1 * MS);
    EXPECT_NEAR(o.yaw_rad, M_PI / 2.0, 2e-3);
    EXPECT_NEAR(o.x_m, 1.0, 2e-3);
    EXPECT_NEAR(o.y_m, 1.0, 2e-3);
}

TEST(Odometry, LongGapIsNotIntegrated)
{
    oamr_odom_t o;
    oamr_odom_init(&o);
    drive(&o, 5.0f, 5.0f, 0, 1000 * MS);
    const double x = o.x_m;
    EXPECT_FALSE(oamr_odom_update(&o, 5.0f, 5.0f, 2000 * MS, R, W, 500 * MS));
    EXPECT_EQ(o.gaps, 1u);
    EXPECT_DOUBLE_EQ(o.x_m, x);
    // Integration resumes from the new stamp.
    EXPECT_TRUE(oamr_odom_update(&o, 5.0f, 5.0f, 2100 * MS, R, W, 500 * MS));
    EXPECT_NEAR(o.x_m, x + 0.05, 1e-9);
}

TEST(Odometry, StaleOrDuplicateStampIgnored)
{
    oamr_odom_t o;
    oamr_odom_init(&o);
    drive(&o, 5.0f, 5.0f, 0, 100 * MS);
    const double x = o.x_m;
    EXPECT_FALSE(oamr_odom_update(&o, 5.0f, 5.0f, 100 * MS, R, W, 500 * MS));
    EXPECT_FALSE(oamr_odom_update(&o, 5.0f, 5.0f, 50 * MS, R, W, 500 * MS));
    EXPECT_DOUBLE_EQ(o.x_m, x);
}

TEST(Odometry, RefusesWithoutGeometryOrWithNaN)
{
    oamr_odom_t o;
    oamr_odom_init(&o);
    EXPECT_FALSE(oamr_odom_update(&o, 1.0f, 1.0f, 0, NAN, W, 500 * MS));
    EXPECT_FALSE(oamr_odom_update(&o, 1.0f, 1.0f, 0, R, 0.0f, 500 * MS));
    EXPECT_FALSE(oamr_odom_update(&o, NAN, 1.0f, 0, R, W, 500 * MS));
    EXPECT_FALSE(o.valid);
}

}  // namespace
