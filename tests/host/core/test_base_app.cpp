// SPDX-License-Identifier: MIT
// Base controller application against the fake ZLAC (CAN1), Daly replies
// (CAN2) and the fake IMU register file: boot, the RUN gate, command to
// 60FFh, odometry from 606Ch, the status fault list.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "fake_can.h"
#include "fake_spi.h"
#include "test_params.h"

extern "C" {
#include "oamr/base_app.h"
}

namespace {

constexpr uint64_t MS = 1000;

uint64_t g_now = 0;
uint64_t clock_now(void *) { return g_now; }

class App : public ::testing::Test {
protected:
    void SetUp() override { g_now = 0; }
    void boot(bool commissioned = true)
    {
        oamr_app_board_t b{};
        can1_tx = can1.tx();
        can2_tx = can2.tx();
        spi = fake_spi.spi();
        b.can1 = can1_tx;
        b.can2 = can2_tx;
        b.imu_spi = spi;
        oamr_commissioning_record_init(&b.commissioning);
        if (commissioned) {
            b.commissioning.wheel_radius_m = kTestWheelRadiusM;
            b.commissioning.track_width_m = kTestTrackWidthM;
            b.commissioning.accel_limit_m_s2 = 100.0f;  // no ramp in these tests
            b.commissioning.zlac_accel_time_ms = 500;
            b.commissioning.zlac_decel_time_ms = 500;
            b.commissioning.zlac_comm_loss_protection_ms = 250;
            b.commissioning.zlac_heartbeat_period_ms = 100;
            b.commissioning.zlac_heartbeat_misses = 3;
        }
        b.boot_id = 0x1234;
        b.reset_cause = OAMR_RESET_POWER_ON;
        b.now_us = clock_now;
        oamr_app_init(&app, &b, g_now);
        gpio.gpio_valid = true;
        gpio.safety_ok = false;
        gpio.estop_column = OAMR_SC_COLUMN_RELEASED;
        gpio.estop_column_seen = true;
    }
    void pump()
    {
        while (!can1.pending.empty()) {
            const oamr_can_frame_t f = can1.pending.front();
            can1.pending.pop_front();
            for (const auto &r : node.handle(f)) oamr_app_on_can1_frame(&app, &r, g_now);
        }
        while (!can2.pending.empty()) {
            const oamr_can_frame_t f = can2.pending.front();
            can2.pending.pop_front();
            if (bms_alive) daly_reply(f);
        }
    }
    void daly_reply(const oamr_can_frame_t &req)
    {
        const uint8_t id = static_cast<uint8_t>((req.id >> 16) & 0xFF);
        std::vector<uint8_t> d;
        switch (id) {
        case 0x90: d = {0x01, 0x00, 0x01, 0x00, 0x75, 0x3A, 0x03, 0x84}; break;
        case 0x91: d = {0x0C, 0xE4, 0x03, 0x0C, 0xD0, 0x07, 0, 0}; break;
        case 0x92: d = {65, 1, 60, 2, 0, 0, 0, 0}; break;
        case 0x93: d = {1, 1, 1, 7, 0, 0, 0x27, 0x10}; break;
        case 0x98: d = bms_failure; break;
        default: return;
        }
        const oamr_can_frame_t f = make_frame(oamr_daly_response_id(id), d, true);
        oamr_app_on_can2_frame(&app, &f, g_now);
    }
    // One 1 ms control tick, host alive, PNOZ modelled as in test_safety_core.
    void tick()
    {
        g_now += MS;
        if (host_alive) oamr_app_host_heartbeat(&app, g_now);
        if (g_now - last_hb >= 100 * MS) {
            const auto f = node.heartbeat();
            oamr_app_on_can1_frame(&app, &f, g_now);
            last_hb = g_now;
        }
        gpio.estop_column_rx_us = g_now;
        if (prev_reset && !gpio.reset_pressed && !gpio.estop_base_active) gpio.safety_ok = true;
        if (gpio.estop_base_active) gpio.safety_ok = false;
        prev_reset = gpio.reset_pressed;
        if (imu_alive) fake_spi.sample(100, 0, 0, 2048, 0, 0, 0);
        last = oamr_app_control_tick(&app, g_now, &gpio, false, false);
        oamr_app_background(&app, g_now);
        pump();
    }
    void run(uint64_t ms)
    {
        for (uint64_t i = 0; i < ms; i++) tick();
    }
    void to_run()
    {
        run(400);
        gpio.reset_pressed = true;
        run(200);
        gpio.reset_pressed = false;
        run(50);
        oamr_app_sw_stop(&app, false);
        run(10);
        ASSERT_EQ(app.safety.out.state, OAMR_SC_RUN);
    }
    oamr_app_status_t status()
    {
        oamr_app_status_t s;
        oamr_app_snapshot(&app, g_now, &s);
        return s;
    }
    static bool has(const oamr_app_status_t &s, uint16_t code)
    {
        for (uint8_t i = 0; i < s.n_faults; i++)
            if (s.faults[i].code == code) return true;
        return false;
    }

    oamr_app_t app{};
    FakeCanTx can1, can2;
    oamr_can_tx_t can1_tx{}, can2_tx{};
    FakeZlac node;
    FakeSpi fake_spi;
    oamr_spi_t spi{};
    oamr_app_gpio_t gpio{};
    oamr_app_actions_t last{};
    uint64_t last_hb = 0;
    bool host_alive = true, bms_alive = true, imu_alive = true, prev_reset = false;
    std::vector<uint8_t> bms_failure = {0, 0, 0, 0, 0, 0, 0, 0};
};

TEST_F(App, BootsLockedWithSwStopAndReportsInhibit)
{
    boot();
    run(400);
    const auto s = status();
    EXPECT_EQ(s.contract_version, OAMR_APP_CONTRACT_VERSION);
    EXPECT_EQ(s.boot_id, 0x1234u);
    EXPECT_EQ(s.safety_state, OAMR_SC_LOCKED);
    EXPECT_TRUE(s.recovery_required);
    EXPECT_FALSE(s.motion_allowed);
    EXPECT_TRUE(s.imu_present);
    EXPECT_TRUE(s.bms_fresh);
    EXPECT_EQ(s.drive_state, OAMR_ZLAC_OPERATIONAL);
    EXPECT_TRUE(has(s, OAMR_FAULT_SAFETY_INHIBIT));
    EXPECT_FALSE(has(s, OAMR_FAULT_NOT_COMMISSIONED));
    EXPECT_EQ(s.controller_state, OAMR_CTRL_DEGRADED);
}

TEST_F(App, CommandReachesDriveOnlyInRun)
{
    boot();
    run(400);
    oamr_app_cmd_vel(&app, 0.5f, 0.0f, g_now);
    run(100);
    EXPECT_EQ(app.motion.applied.state, OAMR_APPLIED_INHIBITED);
    EXPECT_EQ(node.get(0x60FF, 1), 0u);

    to_run();
    for (int i = 0; i < 20; i++) {
        oamr_app_cmd_vel(&app, 0.5f, 0.0f, g_now);
        run(10);
    }
    const auto s = status();
    EXPECT_EQ(s.applied.state, OAMR_APPLIED_OK);
    EXPECT_TRUE(s.command_accepted);
    EXPECT_TRUE(s.motion_allowed);
    // 0.5 m/s on a 0.1 m wheel = 5 rad/s = 47.7 rpm; left mirrored.
    const int32_t left = static_cast<int32_t>(node.get(0x60FF, 1));
    const int32_t right = static_cast<int32_t>(node.get(0x60FF, 2));
    EXPECT_NEAR(std::abs(left), 48, 1);
    EXPECT_NEAR(std::abs(right), 48, 1);
    EXPECT_EQ(s.n_faults, 0u) << oamr_app_fault_name(s.faults[0].code);
    EXPECT_EQ(s.controller_state, OAMR_CTRL_READY);
}

TEST_F(App, CommandTimeoutZeroesAndIsReported)
{
    boot();
    to_run();
    oamr_app_cmd_vel(&app, 0.5f, 0.0f, g_now);
    run(300);
    const auto s = status();
    EXPECT_EQ(s.applied.state, OAMR_APPLIED_TIMEOUT);
    EXPECT_TRUE(has(s, OAMR_FAULT_CMD_TIMEOUT));
    EXPECT_EQ(node.get(0x60FF, 1), 0u);
    EXPECT_EQ(node.get(0x60FF, 2), 0u);
}

TEST_F(App, SwStopLatchesBackToLocked)
{
    boot();
    to_run();
    oamr_app_sw_stop(&app, true);
    run(10);
    EXPECT_EQ(status().safety_state, OAMR_SC_LOCKED);
    oamr_app_sw_stop(&app, false);
    run(10);
    EXPECT_EQ(status().safety_state, OAMR_SC_LOCKED);  // needs RESET again
}

TEST_F(App, OdometryFollowsMeasuredWheelSpeed)
{
    boot();
    to_run();
    // Drive reports 48 rpm on both wheels. Wheel inversion is not ruled yet
    // (zlac8015d.c invert_left/right TODO), so both read positive here.
    node.set(0x606C, 1, 480u);
    node.set(0x606C, 2, 480u);
    const double x0 = app.odom.x_m;
    run(1000);
    const auto s = status();
    ASSERT_TRUE(s.odom.valid);
    EXPECT_GT(s.odom.x_m - x0, 0.2);
    EXPECT_NEAR(s.odom.y_m, 0.0, 1e-6);
    EXPECT_GT(s.odom.linear_m_s, 0.0f);
}

TEST_F(App, HostLossAndBmsLossReported)
{
    boot();
    to_run();
    host_alive = false;
    bms_alive = false;
    run(600);
    auto s = status();
    EXPECT_TRUE(has(s, OAMR_FAULT_HOST_LINK_LOST));
    EXPECT_EQ(s.safety_state, OAMR_SC_LOCKED);
    run(3000);
    s = status();
    EXPECT_TRUE(has(s, OAMR_FAULT_BMS_STALE));
}

TEST_F(App, BmsLevel2AlarmIsFault)
{
    bms_failure = {0x02, 0, 0, 0, 0, 0, 0, 0};
    boot();
    run(2000);
    const auto s = status();
    EXPECT_TRUE(s.bms.fault);
    EXPECT_TRUE(has(s, OAMR_FAULT_BMS_FAULT));
}

TEST_F(App, DriveFaultLatchesFaulted)
{
    boot();
    to_run();
    node.fault_clearable = false;
    node.inject_fault(0x0001);
    run(500);
    const auto s = status();
    EXPECT_TRUE(has(s, OAMR_FAULT_DRIVE_FAULT));
    EXPECT_EQ(s.controller_state, OAMR_CTRL_FAULTED);
    EXPECT_NE(s.drive_fault_raw, 0u);
}

TEST_F(App, NoCommissioningNoDriveNoMotion)
{
    boot(false);
    run(400);
    const auto s = status();
    EXPECT_FALSE(s.drive_enabled);
    EXPECT_TRUE(has(s, OAMR_FAULT_NOT_COMMISSIONED));
    EXPECT_EQ(s.commissioning_missing, static_cast<uint32_t>(OAMR_COMM_ALL));
    EXPECT_TRUE(can1.sent.empty());
}

TEST_F(App, ImuMissingReportedButNotInhibiting)
{
    fake_spi.regs[0x75] = 0x00;
    boot();
    run(100);
    const auto s = status();
    EXPECT_FALSE(s.imu_present);
    bool found = false;
    for (uint8_t i = 0; i < s.n_faults; i++) {
        if (s.faults[i].code == OAMR_FAULT_IMU_NOT_PRESENT) {
            found = true;
            EXPECT_FALSE(s.faults[i].inhibits_motion);
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(App, FaultListIsSortedAndBounded)
{
    boot(false);
    host_alive = false;
    bms_alive = false;
    fake_spi.regs[0x75] = 0x00;
    run(4000);
    const auto s = status();
    EXPECT_LE(s.n_faults, OAMR_APP_MAX_FAULTS);
    for (uint8_t i = 1; i < s.n_faults; i++) EXPECT_LT(s.faults[i - 1].code, s.faults[i].code);
}

}  // namespace
