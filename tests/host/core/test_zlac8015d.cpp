// SPDX-License-Identifier: MIT
// ZLAC8015D adapter against a fake CANopen node: SDO client, boot
// configuration with read-back, save only when changed, enable sequence,
// cyclic velocity write and feedback, 603Fh fault decode, heartbeat consumer.
#include <gtest/gtest.h>

#include <cmath>

#include "fake_can.h"

extern "C" {
#include "oamr/canopen_sdo.h"
#include "oamr/zlac8015d.h"
}

namespace {

constexpr uint64_t MS = 1000;

class Zlac : public ::testing::Test {
protected:
    void SetUp() override
    {
        oamr_zlac_default_config(&cfg);
        cfg.heartbeat_period_ms = 100;
    }
    void start()
    {
        tx = can.tx();
        oamr_zlac_init(&z, &cfg, &tx, now);
    }
    // Deliver frames between master and node until the bus is quiet.
    void pump()
    {
        while (!can.pending.empty()) {
            const oamr_can_frame_t f = can.pending.front();
            can.pending.pop_front();
            for (const auto &r : node.handle(f)) {
                oamr_zlac_on_frame(&z, &r, now);
            }
        }
    }
    void heartbeat() { const auto f = node.heartbeat(); oamr_zlac_on_frame(&z, &f, now); }
    // One control tick of `dt` with a heartbeat every 100 ms.
    void step(uint64_t dt = 20 * MS, bool hb = true)
    {
        now += dt;
        if (hb && now - last_hb >= 100 * MS) {
            heartbeat();
            last_hb = now;
        }
        oamr_zlac_tick(&z, now);
        pump();
    }
    void run(uint64_t duration, bool hb = true)
    {
        const uint64_t end = now + duration;
        while (now < end) step(20 * MS, hb);
    }
    void boot_to_operational()
    {
        start();
        run(200 * MS);
        ASSERT_EQ(z.state, OAMR_ZLAC_OPERATIONAL);
        run(40 * MS);
        ASSERT_TRUE(oamr_zlac_ready(&z, now));
    }

    oamr_zlac_config_t cfg{};
    oamr_zlac_t z{};
    FakeCanTx can;
    oamr_can_tx_t tx{};
    FakeZlac node;
    uint64_t now = 0;
    uint64_t last_hb = 0;
};

TEST(Sdo, ExpeditedFramesAndAbort)
{
    FakeCanTx can;
    oamr_can_tx_t tx = can.tx();
    oamr_sdo_client_t c;
    oamr_sdo_init(&c, 1, 50 * MS);
    ASSERT_TRUE(oamr_sdo_start_write(&c, &tx, 0x2000, 0, 250, 2, 0));
    const oamr_can_frame_t &w = can.sent.back();
    EXPECT_EQ(w.id, 0x601u);
    const uint8_t expect_w[8] = {0x2B, 0x00, 0x20, 0x00, 0xFA, 0x00, 0x00, 0x00};
    EXPECT_EQ(0, std::memcmp(w.data, expect_w, 8));
    EXPECT_FALSE(oamr_sdo_start_read(&c, &tx, 0x6041, 0, 0));  // one outstanding transfer
    // An answer for another object is ignored.
    auto other = make_frame(0x581, {0x60, 0x40, 0x60, 0x00, 0, 0, 0, 0});
    oamr_sdo_on_frame(&c, &other);
    EXPECT_EQ(c.state, OAMR_SDO_BUSY);
    auto abort = make_frame(0x581, {0x80, 0x00, 0x20, 0x00, 0x00, 0x00, 0x02, 0x06});
    oamr_sdo_on_frame(&c, &abort);
    EXPECT_EQ(c.state, OAMR_SDO_ABORTED);
    EXPECT_EQ(c.abort_code, 0x06020000u);
    oamr_sdo_release(&c);

    ASSERT_TRUE(oamr_sdo_start_read(&c, &tx, 0x6041, 0, 0));
    EXPECT_EQ(can.sent.back().data[0], 0x40);
    auto up = make_frame(0x581, {0x4B, 0x41, 0x60, 0x00, 0x37, 0x02, 0xAA, 0xBB});
    oamr_sdo_on_frame(&c, &up);
    EXPECT_EQ(c.state, OAMR_SDO_DONE);
    EXPECT_EQ(c.value, 0x0237u);  // two bytes only
    EXPECT_EQ(c.size, 2);
}

TEST(Sdo, TimeoutSendsAbort)
{
    FakeCanTx can;
    oamr_can_tx_t tx = can.tx();
    oamr_sdo_client_t c;
    oamr_sdo_init(&c, 1, 50 * MS);
    ASSERT_TRUE(oamr_sdo_start_read(&c, &tx, 0x603F, 0, 0));
    oamr_sdo_poll(&c, &tx, 49 * MS);
    EXPECT_EQ(c.state, OAMR_SDO_BUSY);
    oamr_sdo_poll(&c, &tx, 50 * MS);
    EXPECT_EQ(c.state, OAMR_SDO_TIMEOUT);
    EXPECT_EQ(can.sent.back().data[0], 0x80);
    EXPECT_EQ(can.sent.back().data[7], 0x05);  // 0x05040000 SDO protocol timed out
    EXPECT_FALSE(oamr_sdo_start_write(&c, &tx, 0x2000, 0, 1, 3, 0));  // invalid size
}

TEST_F(Zlac, BootConfigWritesReadsBackAndSavesOnFactoryDrive)
{
    start();
    run(200 * MS);
    EXPECT_EQ(z.state, OAMR_ZLAC_OPERATIONAL);
    // Pre-operational first, then start after configuration.
    ASSERT_GE(node.nmt_commands.size(), 2u);
    EXPECT_EQ(node.nmt_commands.front(), 0x80);
    EXPECT_EQ(node.nmt_commands.back(), 0x01);
    // Values on the drive.
    EXPECT_EQ(node.get(0x6060, 0), 3u);
    EXPECT_EQ(node.get(0x2000, 0), 250u);
    EXPECT_EQ(node.get(0x1017, 0), 100u);
    // 6060h and 2000h and 1017h differ from factory; accel/decel equal factory.
    EXPECT_EQ(z.config_writes, 3u);
    EXPECT_EQ(node.writes_to(0x6083), 0u);
    EXPECT_EQ(node.writes_to(0x2010), 1u);
    EXPECT_EQ(z.saves, 1u);
    // Every written entry is read back after the write.
    bool seen_write = false, seen_readback = false;
    for (const auto &r : node.requests) {
        if (r.index != 0x2000) continue;
        if (r.write) seen_write = true;
        else if (seen_write) seen_readback = true;
    }
    EXPECT_TRUE(seen_readback);
    // Targets zeroed before enable; controlword 06h, 07h, 0Fh in that order.
    std::vector<uint32_t> cw;
    for (const auto &r : node.requests) {
        if (r.write && r.index == 0x6040) cw.push_back(r.value);
    }
    ASSERT_EQ(cw.size(), 3u);
    EXPECT_EQ(cw[0], 0x06u);
    EXPECT_EQ(cw[1], 0x07u);
    EXPECT_EQ(cw[2], 0x0Fu);
}

TEST_F(Zlac, NoSaveWhenDriveAlreadyConfigured)
{
    node.set(0x6060, 0, 3);
    node.set(0x2000, 0, 250);
    node.set(0x1017, 0, 100);
    start();
    run(200 * MS);
    EXPECT_EQ(z.state, OAMR_ZLAC_OPERATIONAL);
    EXPECT_EQ(z.config_writes, 0u);
    EXPECT_EQ(node.writes_to(0x2010), 0u);
    EXPECT_EQ(node.writes_to(0x2000), 0u);
}

TEST_F(Zlac, ReadBackMismatchLatchesConfigFailed)
{
    node.ignore_writes.insert({0x2000, 0});
    start();
    run(300 * MS);
    EXPECT_EQ(z.state, OAMR_ZLAC_CONFIG_FAILED);
    EXPECT_EQ(z.failed_index, 0x2000);
    EXPECT_EQ(node.writes_to(0x2010), 0u);  // nothing saved
    EXPECT_EQ(node.writes_to(0x6040), 0u);  // never enabled
    EXPECT_FALSE(oamr_zlac_ready(&z, now));
    run(2000 * MS);
    EXPECT_EQ(z.state, OAMR_ZLAC_CONFIG_FAILED);  // no silent retry
}

TEST_F(Zlac, SdoAbortDuringConfigLatches)
{
    node.abort_on_access.insert({0x2000, 0});
    start();
    run(300 * MS);
    EXPECT_EQ(z.state, OAMR_ZLAC_CONFIG_FAILED);
    EXPECT_EQ(z.failed_abort_code, 0x06020000u);
    node.abort_on_access.clear();
    oamr_zlac_request_reinit(&z, now);
    run(300 * MS);
    EXPECT_EQ(z.state, OAMR_ZLAC_OPERATIONAL);
}

TEST_F(Zlac, SilentDriveNeverBecomesReady)
{
    node.silent = true;
    start();
    run(2000 * MS, false);
    EXPECT_NE(z.state, OAMR_ZLAC_OPERATIONAL);
    EXPECT_FALSE(oamr_zlac_ready(&z, now));
    EXPECT_GT(z.sdo_failures_total, 0u);
    EXPECT_EQ(node.writes_to(0x6040), 0u);
}

TEST_F(Zlac, CyclicVelocityWriteAndFeedback)
{
    boot_to_operational();
    const float rad_s = 10.0f;  // 95.49 r/min
    oamr_zlac_set_velocity(&z, rad_s, -rad_s);
    node.set(0x606C, 1, 955);
    node.set(0x606C, 2, static_cast<uint32_t>(-955));
    step();
    EXPECT_EQ(static_cast<int32_t>(node.get(0x60FF, 1)), 95);
    EXPECT_EQ(static_cast<int32_t>(node.get(0x60FF, 2)), -95);
    EXPECT_TRUE(z.feedback.valid);
    EXPECT_NEAR(z.feedback.left_rad_s, 10.0f, 0.01f);
    EXPECT_NEAR(z.feedback.right_rad_s, -10.0f, 0.01f);
}

TEST_F(Zlac, InversionAndClamp)
{
    cfg.invert_right = true;
    boot_to_operational();
    oamr_zlac_set_velocity(&z, 1000.0f, 1.0f);
    EXPECT_EQ(z.target_rpm_left, 1000);
    EXPECT_EQ(z.target_rpm_right, -10);
    oamr_zlac_set_velocity(&z, NAN, 1.0f);
    EXPECT_EQ(z.target_rpm_left, 0);
    EXPECT_EQ(z.target_rpm_right, 0);
}

TEST(ZlacFault, DecodePerSide)
{
    // left: over-current + Hall, right: encoder; drive-wide under-voltage in the low half.
    const uint32_t raw = (uint32_t(OAMR_ZLAC_FAULT_OVER_CURRENT | OAMR_ZLAC_FAULT_HALL) << 16) |
                         OAMR_ZLAC_FAULT_ENCODER | OAMR_ZLAC_FAULT_UNDER_VOLTAGE;
    const oamr_zlac_fault_t f = oamr_zlac_decode_fault(raw);
    EXPECT_EQ(f.left, OAMR_ZLAC_FAULT_OVER_CURRENT | OAMR_ZLAC_FAULT_HALL);
    EXPECT_EQ(f.right, OAMR_ZLAC_FAULT_ENCODER);
    EXPECT_EQ(f.drive_wide, OAMR_ZLAC_FAULT_UNDER_VOLTAGE);
    EXPECT_EQ(oamr_zlac_decode_fault(0).left, 0);
}

TEST_F(Zlac, DriveFaultLatchesUntilExplicitReset)
{
    boot_to_operational();
    oamr_zlac_set_velocity(&z, 5.0f, 5.0f);
    node.inject_fault(uint32_t(OAMR_ZLAC_FAULT_MOTOR_OVER_TEMPERATURE) << 16);
    step();
    step();
    EXPECT_EQ(z.state, OAMR_ZLAC_FAULT);
    EXPECT_FALSE(oamr_zlac_ready(&z, now));
    EXPECT_EQ(z.fault.left, OAMR_ZLAC_FAULT_MOTOR_OVER_TEMPERATURE);
    // In FAULT only zero targets are written.
    run(100 * MS);
    EXPECT_EQ(node.get(0x60FF, 1), 0u);
    // The fault condition goes away by itself: still latched.
    node.set(0x603F, 0, 0);
    run(500 * MS);
    EXPECT_EQ(z.state, OAMR_ZLAC_FAULT);
    oamr_zlac_request_fault_reset(&z);
    run(200 * MS);
    EXPECT_EQ(z.state, OAMR_ZLAC_OPERATIONAL);
    bool reset_sent = false;
    for (const auto &r : node.requests) {
        if (r.write && r.index == 0x6040 && r.value == 0x80) reset_sent = true;
    }
    EXPECT_TRUE(reset_sent);
}

TEST_F(Zlac, HeartbeatLossAfterThreeMissedPeriods)
{
    boot_to_operational();
    const uint64_t last = last_hb;
    // No heartbeats from now on.
    while (now < last + 300 * MS - 20 * MS) {
        step(20 * MS, false);
        EXPECT_TRUE(oamr_zlac_ready(&z, now)) << "at " << (now - last) / MS << " ms";
    }
    step(20 * MS, false);
    EXPECT_EQ(z.state, OAMR_ZLAC_COMM_LOST);
    EXPECT_FALSE(oamr_zlac_ready(&z, now));
    EXPECT_EQ(z.heartbeat_losses, 1u);
    // Stays down while the node is silent.
    run(3000 * MS, false);
    EXPECT_EQ(z.state, OAMR_ZLAC_COMM_LOST);
    // Heartbeat returns: full boot sequence again, then ready.
    run(1500 * MS, true);
    EXPECT_EQ(z.state, OAMR_ZLAC_OPERATIONAL);
    EXPECT_GE(z.boot_attempts, 2u);
}

TEST_F(Zlac, BootUpMessageRerunsBootSequence)
{
    boot_to_operational();
    const uint32_t boots = z.boot_attempts;
    node.set(0x2000, 0, 0);  // drive lost unsaved state
    const auto f = node.bootup();
    oamr_zlac_on_frame(&z, &f, now);
    EXPECT_FALSE(oamr_zlac_ready(&z, now));
    run(300 * MS);
    EXPECT_EQ(z.boot_attempts, boots + 1);
    EXPECT_EQ(node.get(0x2000, 0), 250u);
    EXPECT_EQ(z.state, OAMR_ZLAC_OPERATIONAL);
}

TEST_F(Zlac, RepeatedSdoFailuresInOperationDropReady)
{
    boot_to_operational();
    node.silent = true;
    run(400 * MS);  // heartbeats still arrive
    EXPECT_FALSE(oamr_zlac_ready(&z, now));
    EXPECT_EQ(z.state, OAMR_ZLAC_COMM_LOST);
}

TEST_F(Zlac, BackendBindingForMotionCore)
{
    boot_to_operational();
    struct Clk {
        uint64_t *now;
        static uint64_t get(void *c) { return *static_cast<Clk *>(c)->now; }
    } clk{&now};
    oamr_zlac_backend_ctx_t ctx{&z, &Clk::get, &clk};
    oamr_motor_backend_t be = oamr_zlac_backend(&ctx);
    EXPECT_TRUE(be.is_ready(be.ctx));
    be.set_wheel_velocity(be.ctx, 2.0f, 2.0f);
    EXPECT_EQ(z.target_rpm_left, 19);
}

}  // namespace
