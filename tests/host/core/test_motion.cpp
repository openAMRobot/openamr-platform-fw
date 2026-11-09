// SPDX-License-Identifier: MIT
// Core command path: timeout, clamp, ramp, motion_allowed gate, fresh command.
#include <gtest/gtest.h>

#include <cmath>

#include "fake_motor.h"

extern "C" {
#include "oamr/motion.h"
}

namespace {

constexpr uint64_t MS = 1000;
constexpr uint64_t TICK = 20 * MS;  // 50 Hz control loop (Gate A baseline)

class Motion : public ::testing::Test {
protected:
    void SetUp() override
    {
        oamr_motion_default_config(&cfg);
        oamr_motion_init(&m, &cfg);
        be = motor.backend();
    }
    const oamr_motion_applied_t *tick(bool allowed = true, bool hold = false)
    {
        const oamr_motion_applied_t *a = oamr_motion_tick(&m, now, allowed, hold, &be);
        return a;
    }
    // Run ticks with heartbeat and a repeated command until `until`.
    void drive(float v, float w, uint64_t until, bool allowed = true)
    {
        while (now < until) {
            now += TICK;
            oamr_motion_host_heartbeat(&m, now);
            oamr_motion_cmd_vel(&m, v, w, now);
            now += 1;  // command arrives just before the tick
            tick(allowed);
        }
    }
    float wheel_m_s(float rad_s) const { return rad_s * cfg.wheel_radius_m; }

    oamr_motion_config_t cfg{};
    oamr_motion_t m{};
    FakeMotor motor;
    oamr_motor_backend_t be{};
    uint64_t now = 1000 * MS;
};

TEST_F(Motion, DefaultsMatchDecisions)
{
    EXPECT_EQ(cfg.cmd_timeout_us, 200 * MS);
    EXPECT_EQ(cfg.host_timeout_us, 500 * MS);
    EXPECT_FLOAT_EQ(cfg.max_linear_m_s, 0.5f);
    EXPECT_FLOAT_EQ(cfg.max_wheel_m_s, 0.5f);
    EXPECT_FLOAT_EQ(cfg.decel_limit_m_s2, 1.5f);
    EXPECT_FLOAT_EQ(cfg.decel_limit_hold_m_s2, 1.0f);
}

TEST_F(Motion, WritesBackendExactlyOncePerTick)
{
    for (int i = 0; i < 10; i++) {
        now += TICK;
        tick(i % 2 == 0);
    }
    EXPECT_EQ(motor.writes.size(), 10u);
    EXPECT_EQ(m.counters.backend_writes, 10u);
}

TEST_F(Motion, NoMotionWithoutCommand)
{
    oamr_motion_host_heartbeat(&m, now);
    now += TICK;
    EXPECT_EQ(tick()->state, OAMR_APPLIED_NO_COMMAND);
    EXPECT_EQ(motor.last().left, 0.0f);
    EXPECT_EQ(motor.last().right, 0.0f);
}

TEST_F(Motion, FreshCommandMovesAfterGateEntry)
{
    now += TICK;
    tick();  // gate entry at this tick
    drive(0.2f, 0.0f, now + 500 * MS);
    EXPECT_EQ(m.applied.state, OAMR_APPLIED_OK);
    EXPECT_NEAR(wheel_m_s(motor.last().left), 0.2f, 1e-5);
    EXPECT_NEAR(wheel_m_s(motor.last().right), 0.2f, 1e-5);
}

TEST_F(Motion, StaleCommandTimesOutAt200ms)
{
    now += TICK;
    tick();
    drive(0.2f, 0.0f, now + 500 * MS);
    const uint64_t last_cmd = m.cmd_rx_us;
    // Heartbeat keeps going, commands stop.
    now = last_cmd + 199 * MS;
    oamr_motion_host_heartbeat(&m, now);
    EXPECT_EQ(tick()->state, OAMR_APPLIED_OK);
    EXPECT_GT(motor.last().left, 0.0f);
    now = last_cmd + 200 * MS;
    EXPECT_EQ(tick()->state, OAMR_APPLIED_TIMEOUT);
    EXPECT_EQ(motor.last().left, 0.0f);
    EXPECT_EQ(motor.last().right, 0.0f);
}

TEST_F(Motion, TimeoutIsNonLatchingNextFreshCommandResumes)
{
    now += TICK;
    tick();
    drive(0.2f, 0.0f, now + 300 * MS);
    now += 300 * MS;
    oamr_motion_host_heartbeat(&m, now);
    EXPECT_EQ(tick()->state, OAMR_APPLIED_TIMEOUT);
    drive(0.1f, 0.0f, now + 500 * MS);
    EXPECT_EQ(m.applied.state, OAMR_APPLIED_OK);
    EXPECT_NEAR(wheel_m_s(motor.last().left), 0.1f, 1e-5);
}

TEST_F(Motion, TimeoutRunsWithoutAnyHostInput)
{
    // The watchdog is evaluated by the tick alone: no transport call needed.
    now += TICK;
    tick();
    drive(0.3f, 0.0f, now + 400 * MS);
    for (int i = 0; i < 20; i++) {
        now += TICK;
        tick();
    }
    EXPECT_EQ(motor.last().left, 0.0f);
    EXPECT_NE(m.applied.state, OAMR_APPLIED_OK);
}

TEST_F(Motion, HostHeartbeatLossStopsAndDiscardsCommand)
{
    now += TICK;
    tick();
    drive(0.2f, 0.0f, now + 300 * MS);
    const uint64_t hb = m.hb_rx_us;
    // Commands keep arriving but the heartbeat stops.
    while (now < hb + 500 * MS) {
        now += TICK;
        oamr_motion_cmd_vel(&m, 0.2f, 0.0f, now);
        tick();
    }
    EXPECT_EQ(m.applied.state, OAMR_APPLIED_HOST_LOST);
    EXPECT_EQ(motor.last().left, 0.0f);
    // Heartbeat returns: the command received before the return is not used.
    oamr_motion_cmd_vel(&m, 0.2f, 0.0f, now);
    now += 1;
    oamr_motion_host_heartbeat(&m, now);
    now += TICK;
    EXPECT_EQ(tick()->state, OAMR_APPLIED_NO_COMMAND);
    EXPECT_EQ(motor.last().left, 0.0f);
}

TEST_F(Motion, NoHeartbeatEverMeansHostLost)
{
    now += TICK;
    tick();
    oamr_motion_cmd_vel(&m, 0.2f, 0.0f, now + 1);
    now += TICK;
    EXPECT_EQ(tick()->state, OAMR_APPLIED_HOST_LOST);
    EXPECT_EQ(motor.last().left, 0.0f);
}

TEST_F(Motion, LinearClampTo0p5)
{
    float l = 0, r = 0;
    EXPECT_TRUE(oamr_motion_limit(&cfg, 2.0f, 0.0f, &l, &r));
    EXPECT_FLOAT_EQ(l, 0.5f);
    EXPECT_FLOAT_EQ(r, 0.5f);
    EXPECT_TRUE(oamr_motion_limit(&cfg, -2.0f, 0.0f, &l, &r));
    EXPECT_FLOAT_EQ(l, -0.5f);
    EXPECT_FLOAT_EQ(r, -0.5f);
    EXPECT_FALSE(oamr_motion_limit(&cfg, 0.4f, 0.0f, &l, &r));
}

TEST_F(Motion, PureRotationClampedPerWheel)
{
    float l = 0, r = 0;
    // 5 rad/s on a 0.4 m track would need 1.0 m/s per wheel.
    EXPECT_TRUE(oamr_motion_limit(&cfg, 0.0f, 5.0f, &l, &r));
    EXPECT_FLOAT_EQ(l, -0.5f);
    EXPECT_FLOAT_EQ(r, 0.5f);
    EXPECT_TRUE(oamr_motion_limit(&cfg, 0.0f, -5.0f, &l, &r));
    EXPECT_FLOAT_EQ(l, 0.5f);
    EXPECT_FLOAT_EQ(r, -0.5f);
}

TEST_F(Motion, ArcClampKeepsCurvature)
{
    float l = 0, r = 0;
    const float v = 0.5f, w = 1.0f;  // r = 0.7, l = 0.3 before the clamp
    EXPECT_TRUE(oamr_motion_limit(&cfg, v, w, &l, &r));
    EXPECT_FLOAT_EQ(std::max(std::fabs(l), std::fabs(r)), 0.5f);
    EXPECT_NEAR(l / r, 0.3f / 0.7f, 1e-6);
}

TEST_F(Motion, NonFiniteCommandRejected)
{
    oamr_motion_cmd_vel(&m, NAN, 0.0f, now);
    oamr_motion_cmd_vel(&m, 0.0f, INFINITY, now);
    EXPECT_FALSE(m.have_cmd);
    EXPECT_EQ(m.counters.cmd_rejected_nonfinite, 2u);
}

TEST_F(Motion, AccelerationAndDecelerationLimited)
{
    now += TICK;
    oamr_motion_host_heartbeat(&m, now);
    tick();  // gate entry and host link up at this tick
    // Step to 0.5 m/s: at 1.5 m/s^2 and 20 ms ticks the wheel gains 0.03 m/s per tick.
    oamr_motion_cmd_vel(&m, 0.5f, 0.0f, now + 1);
    now += TICK;
    oamr_motion_host_heartbeat(&m, now);
    tick();
    EXPECT_NEAR(wheel_m_s(motor.last().left), 0.03f, 1e-4);
    drive(0.5f, 0.0f, now + 1000 * MS);
    EXPECT_NEAR(wheel_m_s(motor.last().left), 0.5f, 1e-4);
    // Command 0: deceleration at most 1.5 m/s^2.
    float prev = wheel_m_s(motor.last().left);
    for (int i = 0; i < 5; i++) {
        now += TICK;
        oamr_motion_host_heartbeat(&m, now);
        oamr_motion_cmd_vel(&m, 0.0f, 0.0f, now);
        now += 1;
        tick();
        const float cur = wheel_m_s(motor.last().left);
        EXPECT_LE(prev - cur, 1.5f * (TICK + 1) * 1e-6f + 1e-4f);
        EXPECT_GT(prev - cur, 0.0f);
        prev = cur;
    }
}

TEST_F(Motion, HoldAndMoveUsesLowerDeceleration)
{
    now += TICK;
    tick();
    drive(0.5f, 0.0f, now + 1000 * MS);
    now += TICK;
    oamr_motion_host_heartbeat(&m, now);
    oamr_motion_cmd_vel(&m, 0.0f, 0.0f, now);
    const float before = wheel_m_s(motor.last().left);
    const uint64_t last_tick = m.last_tick_us;
    now += TICK;
    oamr_motion_tick(&m, now, true, true, &be);
    const float dt = static_cast<float>(now - last_tick) * 1e-6f;
    EXPECT_NEAR(before - wheel_m_s(motor.last().left), 1.0f * dt, 1e-4);
}

TEST_F(Motion, RampKeepsWheelRatio)
{
    now += TICK;
    oamr_motion_host_heartbeat(&m, now);
    tick();
    now += TICK;
    oamr_motion_host_heartbeat(&m, now);
    oamr_motion_cmd_vel(&m, 0.3f, 0.5f, now);  // l = 0.2, r = 0.4
    now += TICK;
    tick();
    EXPECT_NEAR(motor.last().left / motor.last().right, 0.5f, 1e-5);
}

TEST_F(Motion, MotionAllowedFalseForcesZeroEveryTick)
{
    now += TICK;
    tick();
    drive(0.4f, 0.0f, now + 1000 * MS);
    ASSERT_GT(motor.last().left, 0.0f);
    for (int i = 0; i < 10; i++) {
        now += TICK;
        oamr_motion_host_heartbeat(&m, now);
        oamr_motion_cmd_vel(&m, 0.4f, 0.0f, now);
        now += 1;
        EXPECT_EQ(tick(false)->state, OAMR_APPLIED_INHIBITED);
        EXPECT_EQ(motor.last().left, 0.0f);
        EXPECT_EQ(motor.last().right, 0.0f);
    }
}

TEST_F(Motion, StaleCommandsDiscardedWhenMotionBecomesAllowed)
{
    // Commands arrive while motion is not allowed.
    for (int i = 0; i < 5; i++) {
        now += TICK;
        oamr_motion_host_heartbeat(&m, now);
        oamr_motion_cmd_vel(&m, 0.4f, 0.0f, now);
        tick(false);
    }
    // A command received before the gate opens is never used.
    oamr_motion_cmd_vel(&m, 0.4f, 0.0f, now + 5);
    now += TICK;
    oamr_motion_host_heartbeat(&m, now);
    EXPECT_EQ(tick(true)->state, OAMR_APPLIED_NO_COMMAND);
    EXPECT_EQ(motor.last().left, 0.0f);
    now += TICK;
    oamr_motion_host_heartbeat(&m, now);
    EXPECT_EQ(tick(true)->state, OAMR_APPLIED_NO_COMMAND);
    EXPECT_EQ(motor.last().left, 0.0f);
    // The first command after the entry time moves the robot.
    oamr_motion_cmd_vel(&m, 0.4f, 0.0f, now + 1);
    now += TICK;
    EXPECT_EQ(tick(true)->state, OAMR_APPLIED_OK);
    EXPECT_GT(motor.last().left, 0.0f);
}

TEST_F(Motion, CommandReceivedAtEntryTickIsNotUsed)
{
    now += TICK;
    oamr_motion_host_heartbeat(&m, now);
    oamr_motion_cmd_vel(&m, 0.4f, 0.0f, now);
    EXPECT_EQ(tick(true)->state, OAMR_APPLIED_NO_COMMAND);
}

TEST_F(Motion, DriverNotReadyStopsAndRequiresFreshCommand)
{
    now += TICK;
    tick();
    drive(0.3f, 0.0f, now + 500 * MS);
    motor.ready = false;
    now += TICK;
    oamr_motion_host_heartbeat(&m, now);
    oamr_motion_cmd_vel(&m, 0.3f, 0.0f, now);
    EXPECT_EQ(tick()->state, OAMR_APPLIED_DRIVER_NOT_READY);
    EXPECT_EQ(motor.last().left, 0.0f);
    motor.ready = true;
    now += TICK;
    oamr_motion_host_heartbeat(&m, now);
    EXPECT_EQ(tick()->state, OAMR_APPLIED_NO_COMMAND);
    EXPECT_EQ(motor.last().left, 0.0f);
}

TEST_F(Motion, AppliedCommandReportsAgeAndRadPerSecond)
{
    now += TICK;
    tick();
    drive(0.2f, 0.0f, now + 500 * MS);
    EXPECT_EQ(m.applied.cmd_age_us, 1u);
    EXPECT_NEAR(m.applied.left_rad_s, 0.2f / cfg.wheel_radius_m, 1e-4);
}

}  // namespace
