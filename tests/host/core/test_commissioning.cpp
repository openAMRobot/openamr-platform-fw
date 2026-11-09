// SPDX-License-Identifier: MIT
// Commissioning record: the build refuses to move while wheel radius, track
// width, acceleration limit or the ZLAC8015D ramp and link-loss settings are
// placeholders (review of platform-fw #37, item 2 and 3).
#include <gtest/gtest.h>

#include <cmath>

#include "fake_motor.h"

extern "C" {
#include "oamr/commissioning.h"
#include "oamr/motion.h"
#include "oamr/zlac8015d.h"
}

namespace {

constexpr uint64_t MS = 1000;

// A complete record with test numbers. These are NOT hardware values.
oamr_commissioning_record_t complete_record()
{
    oamr_commissioning_record_t r;
    oamr_commissioning_record_init(&r);
    r.wheel_radius_m = 0.1f;
    r.track_width_m = 0.4f;
    r.accel_limit_m_s2 = 1.5f;
    r.zlac_accel_time_ms = 400;
    r.zlac_decel_time_ms = 300;
    r.zlac_comm_loss_protection_ms = 250;
    r.zlac_heartbeat_period_ms = 100;
    r.zlac_heartbeat_misses = 3;
    return r;
}

// The default motion configuration carries no geometry and no acceleration.
TEST(Commissioning, MotionDefaultsHaveNoPlaceholders)
{
    oamr_motion_config_t mc;
    oamr_motion_default_config(&mc);
    EXPECT_FALSE(mc.commissioned);
    EXPECT_TRUE(std::isnan(mc.wheel_radius_m));
    EXPECT_TRUE(std::isnan(mc.track_width_m));
    EXPECT_TRUE(std::isnan(mc.accel_limit_m_s2));
}

TEST(Commissioning, EmptyRecordMissesEverything)
{
    oamr_commissioning_record_t r;
    oamr_commissioning_record_init(&r);
    EXPECT_EQ(oamr_commissioning_missing(&r), OAMR_COMM_ALL);
    EXPECT_FALSE(oamr_commissioning_complete(&r));
}

TEST(Commissioning, CompleteRecordMissesNothing)
{
    const auto r = complete_record();
    EXPECT_EQ(oamr_commissioning_missing(&r), 0u);
    EXPECT_TRUE(oamr_commissioning_complete(&r));
}

TEST(Commissioning, EachMotionParameterIsChecked)
{
    auto r = complete_record();
    r.wheel_radius_m = NAN;
    EXPECT_EQ(oamr_commissioning_missing(&r), static_cast<uint32_t>(OAMR_COMM_WHEEL_RADIUS));
    r = complete_record();
    r.wheel_radius_m = 0.0f;
    EXPECT_EQ(oamr_commissioning_missing(&r), static_cast<uint32_t>(OAMR_COMM_WHEEL_RADIUS));
    r = complete_record();
    r.track_width_m = -0.4f;
    EXPECT_EQ(oamr_commissioning_missing(&r), static_cast<uint32_t>(OAMR_COMM_TRACK_WIDTH));
    r = complete_record();
    r.accel_limit_m_s2 = INFINITY;
    EXPECT_EQ(oamr_commissioning_missing(&r), static_cast<uint32_t>(OAMR_COMM_ACCEL_LIMIT));
}

TEST(Commissioning, ZlacRampsAreChecked)
{
    auto r = complete_record();
    r.zlac_decel_time_ms = 0;
    EXPECT_EQ(oamr_commissioning_missing(&r), static_cast<uint32_t>(OAMR_COMM_ZLAC_RAMPS));
    r = complete_record();
    r.zlac_accel_time_ms = 0;
    EXPECT_EQ(oamr_commissioning_missing(&r), static_cast<uint32_t>(OAMR_COMM_ZLAC_RAMPS));
}

// The one place where 2000h and the heartbeat consumer meet.
TEST(Commissioning, LinkLossTimingRelationship)
{
    auto r = complete_record();
    // 2000h unset (factory 0) is the roadmap risk 2 failure: drive keeps the last speed.
    r.zlac_comm_loss_protection_ms = 0;
    EXPECT_EQ(oamr_commissioning_missing(&r), static_cast<uint32_t>(OAMR_COMM_ZLAC_LINK_LOSS));
    // 2000h outside the 200 to 300 ms range.
    r = complete_record();
    r.zlac_comm_loss_protection_ms = 199;
    EXPECT_NE(oamr_commissioning_missing(&r), 0u);
    r.zlac_comm_loss_protection_ms = 301;
    EXPECT_NE(oamr_commissioning_missing(&r), 0u);
    r.zlac_comm_loss_protection_ms = 200;
    EXPECT_EQ(oamr_commissioning_missing(&r), 0u);
    r.zlac_comm_loss_protection_ms = 300;
    EXPECT_EQ(oamr_commissioning_missing(&r), 0u);
    // Producer heartbeat unset.
    r = complete_record();
    r.zlac_heartbeat_period_ms = 0;
    EXPECT_EQ(oamr_commissioning_missing(&r), static_cast<uint32_t>(OAMR_COMM_ZLAC_LINK_LOSS));
    // Vendor rule: consumer time must be more than 2 x 1017h, so at least 3 misses.
    r = complete_record();
    r.zlac_heartbeat_misses = 2;
    EXPECT_EQ(oamr_commissioning_missing(&r), static_cast<uint32_t>(OAMR_COMM_ZLAC_LINK_LOSS));
    r.zlac_heartbeat_misses = 3;
    EXPECT_EQ(oamr_commissioning_missing(&r), 0u);
    EXPECT_EQ(oamr_commissioning_consumer_timeout_ms(&r), 300u);
    // 500 ms EDS default producer period: consumer 1.5 s. Allowed by the
    // check (the owner rules on the value), but reported.
    r.zlac_heartbeat_period_ms = 500;
    EXPECT_EQ(oamr_commissioning_missing(&r), 0u);
    EXPECT_EQ(oamr_commissioning_consumer_timeout_ms(&r), 1500u);
}

TEST(Commissioning, ApplyMotionOnlyCommissionsWhenAllThreeValid)
{
    oamr_motion_config_t mc;
    oamr_motion_default_config(&mc);
    EXPECT_FALSE(mc.commissioned);

    oamr_commissioning_record_t r;
    oamr_commissioning_record_init(&r);
    oamr_commissioning_apply_motion(&r, &mc);
    EXPECT_FALSE(mc.commissioned);

    r = complete_record();
    r.track_width_m = NAN;
    oamr_commissioning_apply_motion(&r, &mc);
    EXPECT_FALSE(mc.commissioned);

    r = complete_record();
    r.wheel_radius_m = 0.125f;
    r.track_width_m = 0.45f;
    r.accel_limit_m_s2 = 1.0f;
    oamr_commissioning_apply_motion(&r, &mc);
    EXPECT_TRUE(mc.commissioned);
    EXPECT_FLOAT_EQ(mc.wheel_radius_m, 0.125f);
    EXPECT_FLOAT_EQ(mc.track_width_m, 0.45f);
    EXPECT_FLOAT_EQ(mc.accel_limit_m_s2, 1.0f);
    // Decisions are not taken from the record.
    EXPECT_FLOAT_EQ(mc.max_linear_m_s, 0.5f);
    EXPECT_FLOAT_EQ(mc.decel_limit_m_s2, 1.5f);
}

TEST(Commissioning, ApplyZlacCopiesRampsAndLinkLoss)
{
    oamr_zlac_config_t zc;
    oamr_zlac_default_config(&zc);
    const auto r = complete_record();
    oamr_zlac_apply_commissioning(&zc, &r);
    EXPECT_EQ(zc.accel_time_ms, 400u);
    EXPECT_EQ(zc.decel_time_ms, 300u);
    EXPECT_EQ(zc.comm_loss_protection_ms, 250u);
    EXPECT_EQ(zc.heartbeat_period_ms, 100u);
    EXPECT_EQ(zc.heartbeat_misses, 3u);
}

// Default motion configuration: whatever the safety core says, zero is written.
TEST(Commissioning, UncommissionedMotionCoreNeverMoves)
{
    oamr_motion_config_t mc;
    oamr_motion_default_config(&mc);
    oamr_motion_t m;
    oamr_motion_init(&m, &mc);
    FakeMotor motor;
    const oamr_motor_backend_t be = motor.backend();
    uint64_t now = 1000 * MS;
    oamr_motion_tick(&m, now, true, false, &be);
    for (int i = 0; i < 50; i++) {
        now += 20 * MS;
        oamr_motion_host_heartbeat(&m, now);
        oamr_motion_cmd_vel(&m, 0.3f, 0.5f, now);
        now += 1;
        const auto *a = oamr_motion_tick(&m, now, true, false, &be);
        EXPECT_EQ(a->state, OAMR_APPLIED_NOT_COMMISSIONED);
        EXPECT_EQ(motor.last().left, 0.0f);
        EXPECT_EQ(motor.last().right, 0.0f);
    }
}

// The same loop moves once the record is complete.
TEST(Commissioning, CommissionedMotionCoreMoves)
{
    oamr_motion_config_t mc;
    oamr_motion_default_config(&mc);
    const auto r = complete_record();
    oamr_commissioning_apply_motion(&r, &mc);
    oamr_motion_t m;
    oamr_motion_init(&m, &mc);
    FakeMotor motor;
    const oamr_motor_backend_t be = motor.backend();
    uint64_t now = 1000 * MS;
    oamr_motion_tick(&m, now, true, false, &be);
    for (int i = 0; i < 50; i++) {
        now += 20 * MS;
        oamr_motion_host_heartbeat(&m, now);
        oamr_motion_cmd_vel(&m, 0.3f, 0.0f, now);
        now += 1;
        oamr_motion_tick(&m, now, true, false, &be);
    }
    EXPECT_EQ(m.applied.state, OAMR_APPLIED_OK);
    EXPECT_GT(motor.last().left, 0.0f);
}

}  // namespace
