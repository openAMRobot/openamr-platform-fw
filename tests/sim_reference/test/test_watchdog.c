/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
/* Command watchdog boundary values on the simulated clock (1 ms resolution). */
#include "simref/simref_timing.h"
#include "tharness.h"

static void drive_with(th_world_t *w)
{
    th_world_init(w, SIMREF_CAUSE_POWER_ON);
    th_world_arm_to_run(w);
    CHECK_EQ(w->c.sm.state, SIMREF_ST_RUN);
    th_world_cmd(w, 0.2, 0.0);
    th_world_run(w, 40000);
    CHECK(w->c.sm.motor_follow);
    CHECK(w->d.target[0] != 0 && w->d.target[1] != 0);
}

static void watchdog_199_fresh(void)
{
    th_world_t w;
    drive_with(&w);
    th_world_cmd(&w, 0.2, 0.0);
    int64_t t0 = w.t;
    th_world_run(&w, (int64_t)(SIMREF_CMD_WATCHDOG_MS - 1) * 1000);
    CHECK_EQ(w.t - t0, (SIMREF_CMD_WATCHDOG_MS - 1) * 1000);
    CHECK(w.c.sm.motor_follow);
    CHECK(!w.c.stale_reported);
}

static void watchdog_200_expires(void)
{
    th_world_t w;
    drive_with(&w);
    th_world_cmd(&w, 0.2, 0.0);
    int64_t t0 = w.t;
    w.first_zero_after_nonzero_us = -1;
    th_world_run(&w, (int64_t)SIMREF_CMD_WATCHDOG_MS * 1000);
    CHECK(!w.c.sm.motor_follow);
    CHECK_EQ(w.c.sm.state, SIMREF_ST_RUN); /* T11 is not latching */
    th_world_run(&w, (int64_t)SIMREF_DRIVE_PERIOD_MS * 1000);
    CHECK_EQ(w.d.target[0], 0);
    CHECK_EQ(w.d.target[1], 0);
    /* zero target written within the watchdog time plus one drive period */
    CHECK(w.first_zero_after_nonzero_us >= 0);
    CHECK(w.first_zero_after_nonzero_us - t0 <= (int64_t)(SIMREF_CMD_WATCHDOG_MS + SIMREF_DRIVE_PERIOD_MS) * 1000);
    CHECK(w.first_zero_after_nonzero_us - t0 >= (int64_t)SIMREF_CMD_WATCHDOG_MS * 1000);
    /* a new command follows again */
    th_world_cmd(&w, 0.1, 0.0);
    th_world_run(&w, 30000);
    CHECK(w.c.sm.motor_follow);
    CHECK(w.d.target[0] != 0);
}

static void watchdog_201_still_zero(void)
{
    th_world_t w;
    drive_with(&w);
    th_world_cmd(&w, 0.2, 0.0);
    th_world_run(&w, (int64_t)(SIMREF_CMD_WATCHDOG_MS + 1) * 1000);
    CHECK(!w.c.sm.motor_follow);
    th_world_run(&w, 500000);
    CHECK_EQ(w.d.target[0], 0);
}

static void replay_does_not_feed_watchdog(void)
{
    th_world_t w;
    drive_with(&w);
    th_world_cmd(&w, 0.2, 0.0);
    uint32_t seq_now = w.c.seq_last[SIMREF_SUP_COUNT];
    for (int i = 0; i < 6; i++) {
        simref_ctrl_host_heartbeat(&w.c, 7, w.t);
        simref_ctrl_host_cmd(&w.c, 7, seq_now, 0.2, 0.0, w.t); /* replay */
        th_world_run(&w, 40000);
    }
    CHECK(!w.c.sm.motor_follow);
    CHECK_EQ(w.d.target[0], 0);
}

int main(void)
{
    RUN(watchdog_199_fresh);
    RUN(watchdog_200_expires);
    RUN(watchdog_201_still_zero);
    RUN(replay_does_not_feed_watchdog);
    return th_report("test_watchdog");
}
