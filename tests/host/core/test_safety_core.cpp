// SPDX-License-Identifier: MIT
// Executable specification of the E-01 safety state machine rev 3,
// verification tests 1 to 30 (platform-fw #17). Proposal for #17 (Evans to
// adopt or rewrite).
//
// The rev 3 test list is known here by its titles only; the numbering T01 to
// T30 follows the order of those titles. Each test states the rule it checks.
// A simple PNOZ model closes the loop: Y32 (SAFETY_OK) goes high on the
// falling edge of RESET when the chain is closed, and drops when an E-stop is
// pressed or KCHG opens the chain.
#include <gtest/gtest.h>

#include "fake_motor.h"

extern "C" {
#include "oamr/motion.h"
#include "oamr/safety_core.h"
}

namespace {

constexpr uint64_t MS = 1000;

class Bench : public ::testing::Test {
protected:
    void SetUp() override
    {
        oamr_sc_default_config(&cfg);
        oamr_sc_init(&sc, &cfg);
        in.gpio_valid = true;
        in.safety_ok = false;
        in.estop_column = OAMR_SC_COLUMN_RELEASED;
        in.estop_column_seen = true;
        in.sw_stop = true;  // asserted at boot
        in.host_seen = true;
        in.bms_seen = true;
        in.encoder_seen = true;
    }

    // One 1 ms tick: refresh live sources, run the PNOZ model, step the core.
    void tick()
    {
        now += MS;
        if (host_alive) in.host_rx_us = now;
        if (bms_alive) in.bms_rx_us = now;
        if (column_alive) in.estop_column_rx_us = now;
        if (encoder_alive) in.encoder_rx_us = now;
        if (pnoz_model) {
            const bool chain_open = !chain_closed || in.estop_base_active ||
                                    in.estop_column == OAMR_SC_COLUMN_PRESSED ||
                                    (out != nullptr && out->charge_request && !kchg_ignored);
            if (chain_open) {
                in.safety_ok = false;
            } else if (prev_reset_raw && !in.reset_pressed) {
                in.safety_ok = true;  // PNOZ s3 resets on the falling edge
            }
        }
        prev_reset_raw = in.reset_pressed;
        out = oamr_sc_step(&sc, &in, now);
    }
    void run(uint64_t ms)
    {
        for (uint64_t i = 0; i < ms; i++) tick();
    }
    void press(uint64_t ms)
    {
        in.reset_pressed = true;
        run(ms);
        in.reset_pressed = false;
        run(50);
    }
    void to_run()
    {
        run(50);
        press(200);
        run(50);
        in.sw_stop = false;
        run(10);
        ASSERT_EQ(out->state, OAMR_SC_RUN);
    }
    void to_charging()
    {
        run(600);  // wheels stopped for more than 500 ms
        in.charge_cmd = true;
        tick();
        ASSERT_EQ(out->state, OAMR_SC_CHARGING);
        run(600);  // KCHG has opened the chain
    }
    bool has(uint32_t reason) const { return (out->reasons & reason) != 0u; }

    oamr_sc_config_t cfg{};
    oamr_sc_t sc{};
    oamr_sc_inputs_t in{};
    const oamr_sc_outputs_t *out = nullptr;
    uint64_t now = 0;
    bool host_alive = true, bms_alive = true, column_alive = true, encoder_alive = true;
    bool pnoz_model = true, chain_closed = true, kchg_ignored = false;
    bool prev_reset_raw = false;
};

// T01: boot is always LOCKED, lamp on, CHARGE_REQUEST off, motion inhibited.
TEST_F(Bench, T01_BootLocked)
{
    tick();
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_FALSE(out->motion_allowed);
    EXPECT_TRUE(out->reset_lamp);
    EXPECT_FALSE(out->charge_request);
    EXPECT_FALSE(out->reset_seen);
    EXPECT_TRUE(has(OAMR_SC_R_SOFTWARE_STOP));
    EXPECT_NE(out->first_reason, 0u);
    // Healthy inputs and Stop released are not enough without a RESET press.
    in.safety_ok = true;
    pnoz_model = false;
    in.sw_stop = false;
    run(2000);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
}

// T02: every transition row.
TEST_F(Bench, T02a_LockedToRunNeedsResetAndStopRelease)
{
    to_run();
    EXPECT_TRUE(out->motion_allowed);
    EXPECT_FALSE(out->reset_lamp);
    EXPECT_EQ(out->reasons, 0u);
}

TEST_F(Bench, T02b_RunToLockedOnSafetyOkLow)
{
    to_run();
    chain_closed = false;
    tick();
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_TRUE(has(OAMR_SC_R_HW_SAFETY_NOT_ENABLED));
    EXPECT_FALSE(out->reset_seen);
}

TEST_F(Bench, T02c_RunToLockedOnEstopDiagnostic)
{
    to_run();
    in.estop_base_active = true;
    tick();
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_TRUE(has(OAMR_SC_R_ESTOP_DIAG));
}

TEST_F(Bench, T02d_RunToLockedOnSoftwareStop)
{
    to_run();
    in.sw_stop = true;
    tick();
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_EQ(out->first_reason, static_cast<uint32_t>(OAMR_SC_R_SOFTWARE_STOP));
}

TEST_F(Bench, T02e_RunToLockedOnBmsFault)
{
    to_run();
    in.bms_fault = true;
    tick();
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_TRUE(has(OAMR_SC_R_BMS_FAULT));
}

TEST_F(Bench, T02f_RunToLockedOnInputInvalid)
{
    to_run();
    in.gpio_valid = false;
    tick();
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_TRUE(has(OAMR_SC_R_INPUT_INVALID));
}

TEST_F(Bench, T02g_LockedToChargingOnOneShotCommand)
{
    run(600);
    in.charge_cmd = true;
    tick();
    EXPECT_EQ(out->state, OAMR_SC_CHARGING);
    EXPECT_TRUE(out->charge_request);
    EXPECT_TRUE(out->reset_lamp);
    EXPECT_FALSE(out->motion_allowed);
}

TEST_F(Bench, T02h_ChargingToLockedOnCommandRemoved)
{
    to_charging();
    in.charge_cmd = false;
    tick();
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_FALSE(out->charge_request);
}

TEST_F(Bench, T02i_ChargingToChargerFaultOnWcm)
{
    to_charging();
    in.wcm_fault = true;
    tick();
    EXPECT_EQ(out->state, OAMR_SC_CHARGER_FAULT);
    EXPECT_FALSE(out->charge_request);
    EXPECT_EQ(out->latches, static_cast<uint32_t>(OAMR_SC_LATCH_CHARGER));
}

TEST_F(Bench, T02j_ChargerFaultToLockedAfterRemovalNormalAndAck)
{
    to_charging();
    in.wcm_fault = true;
    run(10);
    in.charge_cmd = false;
    in.wcm_fault = false;
    run(100);
    EXPECT_EQ(out->state, OAMR_SC_CHARGER_FAULT);
    press(200);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_EQ(out->latches, 0u);
}

// T03: RESET timing boundaries: < 2 s short, 2 s to < 5 s ignored, >= 5 s reboot.
TEST_F(Bench, T03_PressTimingBoundaries)
{
    run(50);
    press(1999);
    EXPECT_TRUE(out->reset_seen);
    EXPECT_FALSE(out->reboot_request);

    in.sw_stop = false;
    run(10);
    ASSERT_EQ(out->state, OAMR_SC_RUN);
    in.sw_stop = true;  // Stop asserted: LOCKED and reset_seen cleared
    run(10);
    ASSERT_FALSE(out->reset_seen);
    press(2000);
    EXPECT_FALSE(out->reset_seen);
    EXPECT_FALSE(out->reboot_request);

    press(4999);
    EXPECT_FALSE(out->reset_seen);
    EXPECT_FALSE(out->reboot_request);

    press(5000);
    EXPECT_TRUE(out->reboot_request);
    EXPECT_TRUE(has(OAMR_SC_R_REBOOT_REQUESTED));
}

// T04: reset window diagnostic: PNOZ did not enable within 1 s.
TEST_F(Bench, T04_ResetWindowDiagnostic)
{
    pnoz_model = false;  // PNOZ never enables
    run(50);
    press(200);
    EXPECT_TRUE(out->reset_window_open);
    EXPECT_TRUE(has(OAMR_SC_R_RESET_IN_PROGRESS));
    run(1000);
    EXPECT_FALSE(out->reset_window_open);
    EXPECT_FALSE(out->reset_seen);
    EXPECT_TRUE(has(OAMR_SC_R_PNOZ_DID_NOT_ENABLE));
}

// T05: reset_seen refused when a condition is not met at the press.
TEST_F(Bench, T05_ResetSeenRefusal)
{
    host_alive = false;
    run(600);
    ASSERT_TRUE(has(OAMR_SC_R_COMMS_LOST));
    press(200);
    EXPECT_FALSE(out->reset_seen);
    EXPECT_FALSE(out->reset_window_open);
    host_alive = true;
    in.sw_stop = false;
    run(500);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);  // comms return never arms
    press(200);
    run(10);
    EXPECT_EQ(out->state, OAMR_SC_RUN);
}

// T06: RESET pressed with an E-stop active, then the E-stop released.
TEST_F(Bench, T06_ResetWithEstopThenRelease)
{
    in.estop_base_active = true;
    run(50);
    press(200);
    EXPECT_FALSE(out->reset_seen);
    in.estop_base_active = false;
    in.sw_stop = false;
    run(500);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_FALSE(out->reset_seen);
    press(200);
    run(10);
    EXPECT_EQ(out->state, OAMR_SC_RUN);
}

// T07: a short press in CHARGER_FAULT acknowledges only and never arms.
TEST_F(Bench, T07_ShortPressInChargerFaultOnlyAcknowledges)
{
    in.sw_stop = false;
    to_charging();
    in.wcm_fault = true;
    run(10);
    in.charge_cmd = false;
    in.wcm_fault = false;
    run(100);
    press(200);  // also resets the PNOZ physically
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_FALSE(out->reset_seen);
    run(1000);
    EXPECT_TRUE(in.safety_ok);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    press(200);
    run(10);
    EXPECT_EQ(out->state, OAMR_SC_RUN);
}

// T08: an open RESET wire reads as held: never a press, never a reboot.
TEST_F(Bench, T08_OpenResetWire)
{
    in.reset_pressed = true;
    in.sw_stop = false;
    run(7000);
    EXPECT_FALSE(out->reboot_request);
    EXPECT_FALSE(out->reset_seen);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_TRUE(has(OAMR_SC_R_RESET_IN_PROGRESS) == false);
    in.reset_pressed = false;  // wire repaired: the release is not a press
    run(500);
    EXPECT_FALSE(out->reset_seen);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    press(200);
    run(10);
    EXPECT_EQ(out->state, OAMR_SC_RUN);
}

// T09: loss of SAFETY_OK, software Stop or comms acts within one tick.
TEST_F(Bench, T09_LossWithinOneTick)
{
    to_run();
    pnoz_model = false;
    in.safety_ok = false;
    tick();
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);

    SetUp();
    pnoz_model = true;
    to_run();
    in.sw_stop = true;
    tick();
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);

    SetUp();
    to_run();
    host_alive = false;
    const uint64_t last = now;
    while (now < last + cfg.comms_timeout_us - MS) {
        tick();
        ASSERT_EQ(out->state, OAMR_SC_RUN);
    }
    tick();
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_TRUE(has(OAMR_SC_R_COMMS_LOST));
}

// T10: stale BMS data locks like a BMS fault.
TEST_F(Bench, T10_StaleBms)
{
    to_run();
    bms_alive = false;
    run(cfg.bms_stale_us / MS - 1);
    EXPECT_EQ(out->state, OAMR_SC_RUN);
    tick();
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_TRUE(has(OAMR_SC_R_BMS_FAULT));
}

// T11: a missing lift-node E-stop observation is an E-stop diagnostic.
TEST_F(Bench, T11_MissingLiftNodeEstop)
{
    to_run();
    column_alive = false;
    run(cfg.column_estop_stale_us / MS);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_TRUE(has(OAMR_SC_R_ESTOP_DIAG));

    SetUp();
    in.estop_column = OAMR_SC_COLUMN_UNKNOWN;  // never reported since boot
    run(50);
    press(200);
    EXPECT_FALSE(out->reset_seen);
    in.estop_column_seen = false;
    in.estop_column = OAMR_SC_COLUMN_RELEASED;
    run(100);
    EXPECT_TRUE(has(OAMR_SC_R_ESTOP_DIAG));
}

// T12: commands received before entry into RUN never move the robot.
TEST_F(Bench, T12_NoMotionFromStaleCommands)
{
    oamr_motion_config_t mc;
    oamr_motion_default_config(&mc);
    oamr_motion_t m;
    oamr_motion_init(&m, &mc);
    FakeMotor motor;
    oamr_motor_backend_t be = motor.backend();
    auto control_tick = [&]() {
        tick();
        oamr_motion_host_heartbeat(&m, now);
        oamr_motion_tick(&m, now, oamr_sc_motion_allowed(&sc), false, &be);
    };
    for (int i = 0; i < 50; i++) control_tick();
    // Commands keep coming while LOCKED.
    in.reset_pressed = true;
    for (int i = 0; i < 200; i++) {
        oamr_motion_cmd_vel(&m, 0.3f, 0.0f, now);
        control_tick();
        EXPECT_EQ(motor.last().left, 0.0f);
    }
    in.reset_pressed = false;
    for (int i = 0; i < 60; i++) {
        oamr_motion_cmd_vel(&m, 0.3f, 0.0f, now);
        control_tick();
    }
    oamr_motion_cmd_vel(&m, 0.3f, 0.0f, now);
    in.sw_stop = false;
    control_tick();
    ASSERT_EQ(out->state, OAMR_SC_RUN);
    EXPECT_EQ(motor.last().left, 0.0f);
    control_tick();
    EXPECT_EQ(motor.last().left, 0.0f);
    EXPECT_EQ(m.applied.state, OAMR_APPLIED_NO_COMMAND);
    oamr_motion_cmd_vel(&m, 0.3f, 0.0f, now);
    control_tick();
    EXPECT_GT(motor.last().left, 0.0f);
}

// T13: WCM_FAULT while charging latches until removal, normal and acknowledgement.
TEST_F(Bench, T13_WcmLatch)
{
    to_charging();
    in.wcm_fault = true;
    tick();
    ASSERT_EQ(out->state, OAMR_SC_CHARGER_FAULT);
    in.wcm_fault = false;
    run(500);
    EXPECT_EQ(out->state, OAMR_SC_CHARGER_FAULT);
    press(200);  // command still present: no acknowledgement
    EXPECT_EQ(out->state, OAMR_SC_CHARGER_FAULT);
    in.charge_cmd = false;
    run(500);
    EXPECT_EQ(out->state, OAMR_SC_CHARGER_FAULT);
    press(200);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
}

// T14: charging refused unless locked, stopped, fault-free and fresh.
TEST_F(Bench, T14_ChargingRefusals)
{
    auto attempt = [&]() {
        in.charge_cmd = true;
        run(20);
        const bool accepted = out->state == OAMR_SC_CHARGING;
        in.charge_cmd = false;
        run(20);
        return accepted;
    };
    run(600);
    in.encoder_left_rpm = 2.0f;  // moving
    run(600);
    EXPECT_FALSE(attempt());
    in.encoder_left_rpm = 0.0f;
    run(300);  // stopped for less than 500 ms
    EXPECT_FALSE(attempt());
    run(600);
    in.estop_base_active = true;
    run(10);
    EXPECT_FALSE(attempt());
    in.estop_base_active = false;
    in.wcm_fault = true;
    run(10);
    EXPECT_FALSE(attempt());
    in.wcm_fault = false;
    in.bms_fault = true;
    run(30);
    EXPECT_FALSE(attempt());
    in.bms_fault = false;
    host_alive = false;
    run(600);
    EXPECT_FALSE(attempt());
    host_alive = true;
    encoder_alive = false;
    run(cfg.encoder_stale_us / MS + 10);
    EXPECT_FALSE(attempt());
    encoder_alive = true;
    run(600);
    in.gpio_valid = false;
    run(10);
    EXPECT_FALSE(attempt());
    in.gpio_valid = true;
    run(600);
    // One-shot: a command raised while refused and then held is never accepted.
    in.estop_base_active = true;
    run(10);
    in.charge_cmd = true;
    run(10);
    in.estop_base_active = false;
    run(600);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    in.charge_cmd = false;
    run(10);
    EXPECT_TRUE(attempt());
}

TEST_F(Bench, T14b_ChargeCommandIgnoredInRun)
{
    to_run();
    in.charge_cmd = true;
    run(100);
    EXPECT_EQ(out->state, OAMR_SC_RUN);
    EXPECT_FALSE(out->charge_request);
}

// T15: with two faults the first reason is kept until RUN; same-tick order is
// hardware safety, then charger fault, then software Stop.
TEST_F(Bench, T15_TwoFaultsKeepFirstReason)
{
    to_run();
    in.sw_stop = true;
    tick();
    in.bms_fault = true;
    run(100);
    EXPECT_EQ(out->first_reason, static_cast<uint32_t>(OAMR_SC_R_SOFTWARE_STOP));
    EXPECT_TRUE(has(OAMR_SC_R_BMS_FAULT));

    SetUp();
    to_run();
    pnoz_model = false;
    in.sw_stop = true;
    in.safety_ok = false;
    tick();
    EXPECT_EQ(out->first_reason, static_cast<uint32_t>(OAMR_SC_R_HW_SAFETY_NOT_ENABLED));
}

// T16: an E-stop while charging ends charging.
TEST_F(Bench, T16_EstopDuringCharging)
{
    to_charging();
    in.estop_base_active = true;
    tick();
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_FALSE(out->charge_request);
    EXPECT_TRUE(has(OAMR_SC_R_ESTOP_DIAG));
}

// T17: charge interlock: SAFETY_OK still high 500 ms after CHARGE_REQUEST, or
// high again later after the 20 ms debounce, latches CHARGER_FAULT.
TEST_F(Bench, T17_ChargeInterlock)
{
    kchg_ignored = true;  // KCHG does not open the chain
    pnoz_model = false;
    in.safety_ok = true;
    run(600);
    in.charge_cmd = true;
    tick();
    ASSERT_EQ(out->state, OAMR_SC_CHARGING);
    run(cfg.charge_interlock_us / MS - 1);
    EXPECT_EQ(out->state, OAMR_SC_CHARGING);
    tick();
    EXPECT_EQ(out->state, OAMR_SC_CHARGER_FAULT);
    EXPECT_EQ(out->latches, static_cast<uint32_t>(OAMR_SC_LATCH_INTERLOCK));
    EXPECT_FALSE(out->charge_request);
}

TEST_F(Bench, T17b_ChargeInterlockHighAgainLater)
{
    to_charging();
    pnoz_model = false;
    in.safety_ok = true;  // 10 ms glitch: ignored
    run(10);
    in.safety_ok = false;
    run(10);
    EXPECT_EQ(out->state, OAMR_SC_CHARGING);
    in.safety_ok = true;  // held: latches after the debounce
    run(19);
    EXPECT_EQ(out->state, OAMR_SC_CHARGING);
    run(2);
    EXPECT_EQ(out->state, OAMR_SC_CHARGER_FAULT);
    EXPECT_EQ(out->latches, static_cast<uint32_t>(OAMR_SC_LATCH_INTERLOCK));
}

// T18: WCM_FAULT asserted at boot is reported and blocks charging; no latch.
TEST_F(Bench, T18_WcmAtBoot)
{
    in.wcm_fault = true;
    run(600);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_TRUE(has(OAMR_SC_R_CHARGER_FAULT));
    EXPECT_EQ(out->latches, 0u);
    in.charge_cmd = true;
    run(10);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_FALSE(out->charge_request);
}

// T19: the controller resets 2 s after the reboot request whether or not the
// Jetson acknowledges.
TEST_F(Bench, T19_JetsonNotAcknowledgingReboot)
{
    run(50);
    in.reset_pressed = true;
    while (!out->reboot_request) {
        tick();
        ASSERT_LT(now, 10000 * MS);
    }
    const uint64_t t = now;
    host_alive = false;  // Jetson never answers
    while (now < t + cfg.controller_reset_delay_us - MS) {
        tick();
        ASSERT_FALSE(out->controller_reset);
    }
    tick();
    EXPECT_TRUE(out->controller_reset);
    EXPECT_TRUE(out->reboot_request);
}

// T20: stale encoder data while charging ends charging.
TEST_F(Bench, T20_EncoderStaleWhileCharging)
{
    to_charging();
    encoder_alive = false;
    run(cfg.encoder_stale_us / MS);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_FALSE(out->charge_request);
}

// T21: a held charge command never restarts charging.
TEST_F(Bench, T21_HeldChargeCommandNeverRestarts)
{
    to_charging();
    in.estop_base_active = true;
    tick();
    ASSERT_EQ(out->state, OAMR_SC_LOCKED);
    in.estop_base_active = false;
    run(2000);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_FALSE(out->charge_request);
    in.charge_cmd = false;
    run(10);
    in.charge_cmd = true;
    tick();
    EXPECT_EQ(out->state, OAMR_SC_CHARGING);
}

// T22: 5 s hold in LOCKED and in CHARGER_FAULT.
TEST_F(Bench, T22_LongHoldInLockedAndChargerFault)
{
    run(50);
    press(5100);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_TRUE(out->reboot_request);
    EXPECT_FALSE(out->reset_seen);

    SetUp();
    to_charging();
    in.wcm_fault = true;
    tick();
    ASSERT_EQ(out->state, OAMR_SC_CHARGER_FAULT);
    in.reset_pressed = true;
    run(4999);
    EXPECT_FALSE(out->reboot_request);
    run(2);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_TRUE(out->reboot_request);
    EXPECT_FALSE(out->charge_request);
    EXPECT_EQ(out->latches, 0u);
}

TEST_F(Bench, T22b_OneRebootRequestPerPress)
{
    run(50);
    in.reset_pressed = true;
    run(5100);
    ASSERT_TRUE(out->reboot_request);
    const uint64_t reset_at = sc.controller_reset_at_us;
    run(5000);  // still held
    EXPECT_EQ(sc.controller_reset_at_us, reset_at);
}

// T23: both latches in the same tick; both must clear.
TEST_F(Bench, T23_SimultaneousLatches)
{
    kchg_ignored = true;
    pnoz_model = false;
    in.safety_ok = true;
    run(600);
    in.charge_cmd = true;
    tick();
    run(cfg.charge_interlock_us / MS - 1);
    in.wcm_fault = true;
    tick();
    EXPECT_EQ(out->state, OAMR_SC_CHARGER_FAULT);
    EXPECT_EQ(out->latches, static_cast<uint32_t>(OAMR_SC_LATCH_CHARGER | OAMR_SC_LATCH_INTERLOCK));
    in.charge_cmd = false;
    run(10);
    press(200);  // WCM still asserted
    EXPECT_EQ(out->state, OAMR_SC_CHARGER_FAULT);
    in.wcm_fault = false;
    run(50);
    press(200);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_EQ(out->latches, 0u);
}

// T24: the interlock latch clears with the command removed and an
// acknowledgement; SAFETY_OK is not required to be low.
TEST_F(Bench, T24_InterlockAckWithSafetyOkHigh)
{
    kchg_ignored = true;
    pnoz_model = false;
    in.safety_ok = true;
    run(600);
    in.charge_cmd = true;
    run(cfg.charge_interlock_us / MS + 5);
    ASSERT_EQ(out->state, OAMR_SC_CHARGER_FAULT);
    in.charge_cmd = false;
    run(10);
    press(200);
    EXPECT_TRUE(in.safety_ok);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_FALSE(out->reset_seen);
}

// T25: unsafe-going changes act at once; safe-going changes must hold 20 ms.
TEST_F(Bench, T25_TwentyMsGlitch)
{
    pnoz_model = false;
    run(50);
    press(200);
    ASSERT_TRUE(out->reset_window_open);
    in.safety_ok = true;  // 19 ms high glitch does not confirm
    run(19);
    in.safety_ok = false;
    tick();
    EXPECT_FALSE(out->reset_seen);
    in.safety_ok = true;  // held 20 ms confirms
    run(21);
    EXPECT_TRUE(out->reset_seen);
    in.sw_stop = false;
    run(5);
    ASSERT_EQ(out->state, OAMR_SC_RUN);
    in.safety_ok = false;  // 1 ms low glitch locks at once
    tick();
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    in.safety_ok = true;
    run(100);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
}

TEST_F(Bench, T25b_EstopReleaseGlitchIgnored)
{
    in.estop_base_active = true;
    run(50);
    in.estop_base_active = false;
    run(10);
    in.estop_base_active = true;
    tick();
    EXPECT_TRUE(has(OAMR_SC_R_ESTOP_DIAG));
    in.estop_base_active = false;
    run(19);
    EXPECT_TRUE(has(OAMR_SC_R_ESTOP_DIAG));
    run(2);
    EXPECT_FALSE(has(OAMR_SC_R_ESTOP_DIAG));
}

// T26: releasing software Stop after it was asserted in RUN needs a fresh press.
TEST_F(Bench, T26_StopReleaseNeedsFreshPress)
{
    to_run();
    in.sw_stop = true;
    tick();
    ASSERT_EQ(out->state, OAMR_SC_LOCKED);
    in.sw_stop = false;
    run(1000);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_FALSE(out->reset_seen);
    press(200);
    run(5);
    EXPECT_EQ(out->state, OAMR_SC_RUN);
}

// T27: after a reboot, RESET while Stop is still held arms; Stop release then enters RUN.
TEST_F(Bench, T27_ResetWhileStopHeldAfterReboot)
{
    run(50);
    press(200);
    EXPECT_TRUE(out->reset_seen);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    run(2000);
    EXPECT_TRUE(out->reset_seen);
    in.sw_stop = false;
    tick();
    EXPECT_EQ(out->state, OAMR_SC_RUN);
}

// T28: expiry of the reset window clears reset_seen; SAFETY_OK rising later never arms.
TEST_F(Bench, T28_WindowExpiryClearsResetSeen)
{
    pnoz_model = false;
    run(50);
    press(200);
    run(1000);
    EXPECT_FALSE(out->reset_window_open);
    EXPECT_FALSE(out->reset_seen);
    in.safety_ok = true;
    in.sw_stop = false;
    run(1000);
    EXPECT_FALSE(out->reset_seen);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
}

// T29: before the first full sample the inputs are INPUT_INVALID.
TEST_F(Bench, T29_BeforeFirstFullSampleInputInvalid)
{
    in.gpio_valid = false;
    run(100);
    EXPECT_TRUE(has(OAMR_SC_R_INPUT_INVALID));
    press(200);
    EXPECT_FALSE(out->reset_seen);
    in.gpio_valid = true;
    run(10);
    EXPECT_FALSE(has(OAMR_SC_R_INPUT_INVALID));
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
}

// T30: 5 s hold in RUN and in CHARGING.
TEST_F(Bench, T30_LongHoldInRunAndCharging)
{
    to_run();
    in.reset_pressed = true;
    run(1000);
    EXPECT_EQ(out->state, OAMR_SC_RUN);  // a press in RUN does nothing by itself
    run(4001);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_TRUE(out->reboot_request);
    EXPECT_EQ(out->transition_reason, static_cast<uint32_t>(OAMR_SC_R_REBOOT_REQUESTED));

    SetUp();
    to_charging();
    in.reset_pressed = true;
    run(5001);
    EXPECT_EQ(out->state, OAMR_SC_LOCKED);
    EXPECT_FALSE(out->charge_request);
    EXPECT_TRUE(out->reboot_request);
}

}  // namespace
