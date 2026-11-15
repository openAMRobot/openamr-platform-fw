/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
/* Enable sequence boundary values, stale heartbeat, drive restart, SDO abort. */
#include "simref/simref_timing.h"
#include "tharness.h"

static void ready(th_world_t *w)
{
    th_world_pins(w, 1, 0, 0, 0, 1);
    simref_ctrl_host_session(&w->c, 7, w->t);
    th_world_run(w, 300000);
    CHECK_EQ(w->c.drive.phase, SIMREF_DL_IDLE);
}

static void enable_with_delay(int64_t delay_ms, bool expect_ok)
{
    th_world_t w;
    th_world_init(&w, SIMREF_CAUSE_POWER_ON);
    ready(&w);
    w.d.transition_delay_us = delay_ms * 1000;
    CHECK(simref_drive_request_enable(&w.c.drive, w.t));
    th_world_run(&w, 1000000);
    if (expect_ok) {
        CHECK_EQ(w.c.drive.phase, SIMREF_DL_ENABLED);
    } else {
        CHECK_EQ(w.c.drive.phase, SIMREF_DL_FAILED);
        CHECK_EQ(w.c.drive.detail, SIMREF_DD_TRANSITION_TIMEOUT);
        CHECK_EQ(w.d.sw_state, 0x21); /* disabled again by the shutdown word */
    }
}

/* the statusword is polled once per SDO round trip (about 1 ms here), so a delay just under
 * the limit passes and a delay at the limit fails */
static void transition_below_limit(void) { enable_with_delay(SIMREF_CIA402_TRANSITION_LIMIT_MS - 5, true); }
static void transition_at_limit(void) { enable_with_delay(SIMREF_CIA402_TRANSITION_LIMIT_MS, false); }
static void transition_above_limit(void) { enable_with_delay(SIMREF_CIA402_TRANSITION_LIMIT_MS + 50, false); }

static void sdo_abort_during_enable(void)
{
    th_world_t w;
    th_world_init(&w, SIMREF_CAUSE_POWER_ON);
    ready(&w);
    w.d.abort_word = 0x0F;
    simref_drive_request_enable(&w.c.drive, w.t);
    th_world_run(&w, 300000);
    CHECK_EQ(w.c.drive.phase, SIMREF_DL_FAILED);
    CHECK_EQ(w.c.drive.detail, SIMREF_DD_SDO_ABORT);
    CHECK_EQ(w.c.drive.last_abort, 0x08000000u);
    CHECK_EQ(w.c.drive.last_abort_index, 0x6040);
    CHECK(w.d.sw_state != 0x27);
    CHECK(th_reason_logged(&w.c, SIMREF_R_DRIVE_CONFIG));
}

static void heartbeat_299_300(void)
{
    th_world_t w;
    th_world_init(&w, SIMREF_CAUSE_POWER_ON);
    ready(&w);
    simref_drive_request_enable(&w.c.drive, w.t);
    th_world_run(&w, 200000);
    CHECK_EQ(w.c.drive.phase, SIMREF_DL_ENABLED);
    /* align: wait for a heartbeat, then go silent */
    int64_t hb = w.c.drive.hb_last_us;
    while (w.c.drive.hb_last_us == hb)
        th_world_run(&w, 1000);
    w.d.silent = true;
    int64_t last = w.c.drive.hb_last_us;
    th_world_run(&w, (int64_t)SIMREF_DRIVE_HB_CONSUMER_MS * 1000 - (w.t - last) - 1000);
    CHECK_EQ(w.t - last, (SIMREF_DRIVE_HB_CONSUMER_MS - 1) * 1000);
    CHECK(w.c.drive.phase != SIMREF_DL_LOST);
    uint32_t writes = w.c.drive.target_writes;
    th_world_run(&w, 1000);
    CHECK_EQ(w.c.drive.phase, SIMREF_DL_LOST);
    CHECK_EQ(w.c.drive.detail, SIMREF_DD_HB_TIMEOUT);
    uint32_t tx_before = w.tx_count[SIMREF_BUS_DRIVE];
    th_world_run(&w, 1000000);
    CHECK_EQ(w.tx_count[SIMREF_BUS_DRIVE], tx_before); /* no further frames, no targets */
    CHECK(w.c.drive.target_writes <= writes + 2);
}

static void restart_needs_new_enable(void)
{
    th_world_t w;
    th_world_init(&w, SIMREF_CAUSE_POWER_ON);
    ready(&w);
    simref_drive_request_enable(&w.c.drive, w.t);
    th_world_run(&w, 200000);
    CHECK_EQ(w.c.drive.phase, SIMREF_DL_ENABLED);
    uint32_t seqs = w.c.drive.enable_sequences;
    uint32_t cw0f = w.controlword_0f_writes;
    th_drive_power_cycle(&w);
    th_world_run(&w, 1000000);
    CHECK_EQ(w.c.drive.phase, SIMREF_DL_IDLE);
    CHECK_EQ(w.c.drive.enable_sequences, seqs);
    CHECK_EQ(w.controlword_0f_writes, cw0f);
    CHECK(w.d.sw_state != 0x27);
    CHECK(simref_drive_request_enable(&w.c.drive, w.t));
    th_world_run(&w, 200000);
    CHECK_EQ(w.c.drive.phase, SIMREF_DL_ENABLED);
}

static void found_enabled_at_boot(void)
{
    th_world_t w;
    th_world_init(&w, SIMREF_CAUSE_SOFTWARE);
    w.d.sw_state = 0x27; /* left enabled by a previous controller */
    w.d.target[0] = w.d.target[1] = 50;
    w.d.booted = true;   /* the drive did not restart */
    w.d.next_hb_us = w.t;
    ready(&w);
    CHECK_EQ(w.c.drive.phase, SIMREF_DL_IDLE);
    CHECK_EQ(w.d.sw_state, 0x21);
    CHECK_EQ(w.d.target[0], 0);
    CHECK_EQ(w.d.target[1], 0);
}

static void config_mismatch(void)
{
    th_world_t w;
    th_world_init(&w, SIMREF_CAUSE_POWER_ON);
    w.d.v2000 = 0; /* link-loss off: not commissioned */
    th_world_pins(&w, 1, 0, 0, 0, 1);
    th_world_run(&w, 300000);
    CHECK_EQ(w.c.drive.phase, SIMREF_DL_FAILED);
    CHECK_EQ(w.c.drive.detail, SIMREF_DD_CONFIG_MISMATCH);
}

int main(void)
{
    RUN(transition_below_limit);
    RUN(transition_at_limit);
    RUN(transition_above_limit);
    RUN(sdo_abort_during_enable);
    RUN(heartbeat_299_300);
    RUN(restart_needs_new_enable);
    RUN(found_enabled_at_boot);
    RUN(config_mismatch);
    return th_report("test_drive");
}
