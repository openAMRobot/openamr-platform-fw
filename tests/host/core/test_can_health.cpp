// SPDX-License-Identifier: MIT
// Bus-off supervision: hold-off, restart, counter, latch after repeated events.
#include <gtest/gtest.h>

extern "C" {
#include "oamr/can_health.h"
}

namespace {

constexpr uint64_t MS = 1000;

class CanHealth : public ::testing::Test {
protected:
    void SetUp() override
    {
        oamr_can_health_default_config(&cfg);
        cfg.restart_delay_us = 100 * MS;
        cfg.max_events = 3;
        cfg.window_us = 10000 * MS;
        oamr_can_health_init(&h, &cfg);
    }
    oamr_can_action_t step(bool bus_off)
    {
        now += 10 * MS;
        return oamr_can_health_update(&h, now, bus_off);
    }
    // One bus-off event followed by a successful restart.
    void one_event()
    {
        step(true);
        for (int i = 0; i < 100 && step(true) != OAMR_CAN_ACTION_RESTART; i++) {
        }
        step(false);
    }
    oamr_can_health_config_t cfg{};
    oamr_can_health_t h{};
    uint64_t now = 0;
};

TEST_F(CanHealth, StaysOkWithoutBusOff)
{
    for (int i = 0; i < 100; i++) {
        EXPECT_EQ(step(false), OAMR_CAN_ACTION_NONE);
    }
    EXPECT_TRUE(oamr_can_health_usable(&h));
    EXPECT_EQ(h.bus_off_total, 0u);
}

TEST_F(CanHealth, RestartOnlyAfterHoldOff)
{
    step(true);
    const uint64_t t0 = now;
    EXPECT_FALSE(oamr_can_health_usable(&h));
    oamr_can_action_t a = OAMR_CAN_ACTION_NONE;
    while (a != OAMR_CAN_ACTION_RESTART) {
        a = step(true);
        ASSERT_LT(now - t0, 1000 * MS);
    }
    EXPECT_GE(now - t0, 100 * MS);
    EXPECT_EQ(h.restarts_total, 1u);
    step(false);
    EXPECT_TRUE(oamr_can_health_usable(&h));
    EXPECT_EQ(h.bus_off_total, 1u);
}

TEST_F(CanHealth, CounterAndLatchAfterRepeatedBusOff)
{
    one_event();
    one_event();
    one_event();
    EXPECT_TRUE(oamr_can_health_usable(&h));
    EXPECT_EQ(h.bus_off_total, 3u);
    step(true);  // fourth event inside the window
    EXPECT_EQ(h.state, OAMR_CAN_FAILED);
    for (int i = 0; i < 100; i++) {
        EXPECT_EQ(step(false), OAMR_CAN_ACTION_NONE);
    }
    EXPECT_EQ(h.state, OAMR_CAN_FAILED);
    EXPECT_EQ(h.bus_off_total, 4u);
}

TEST_F(CanHealth, EventsOutsideWindowDoNotLatch)
{
    one_event();
    one_event();
    one_event();
    now += 20000 * MS;
    one_event();
    EXPECT_TRUE(oamr_can_health_usable(&h));
    EXPECT_EQ(h.bus_off_total, 4u);
}

TEST_F(CanHealth, FailedRestartCountsAsEvent)
{
    step(true);
    while (step(true) != OAMR_CAN_ACTION_RESTART) {
    }
    for (int i = 0; i < 11; i++) {
        step(true);  // still bus-off after the restart
    }
    EXPECT_EQ(h.bus_off_total, 2u);
}

TEST_F(CanHealth, OperatorResetClearsLatch)
{
    for (int i = 0; i < 3; i++) {
        one_event();
    }
    step(true);
    ASSERT_EQ(h.state, OAMR_CAN_FAILED);
    EXPECT_TRUE(oamr_can_health_operator_reset(&h));
    EXPECT_EQ(step(false), OAMR_CAN_ACTION_RESTART);
    step(false);
    EXPECT_TRUE(oamr_can_health_usable(&h));
    EXPECT_FALSE(oamr_can_health_operator_reset(&h));
}

}  // namespace
