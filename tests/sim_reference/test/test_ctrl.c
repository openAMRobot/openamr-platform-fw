/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
/* Controller level: pins to events, reset sequence, simultaneous faults, contactor, reboot. */
#include "simref/simref_timing.h"
#include "tharness.h"

#define reason_logged th_reason_logged

static void arms_to_run(void)
{
    th_world_t w;
    th_world_init(&w, SIMREF_CAUSE_POWER_ON);
    CHECK_EQ(w.c.drive.phase, SIMREF_DL_ABSENT);
    th_world_arm_to_run(&w);
    CHECK_EQ(w.c.sm.state, SIMREF_ST_RUN);
    CHECK_EQ(w.c.drive.phase, SIMREF_DL_ENABLED);
    CHECK_EQ(w.c.drive.enable_sequences, 1);
}

static void no_enable_without_reset(void)
{
    th_world_t w;
    th_world_init(&w, SIMREF_CAUSE_POWER_ON);
    th_world_pins(&w, 1, 0, 0, 0, 1);
    simref_ctrl_host_session(&w.c, 7, w.t);
    for (int i = 0; i < 40; i++) {
        th_world_host_hb(&w);
        th_world_cmd(&w, 0.2, 0);
        th_world_run(&w, 50000);
    }
    CHECK_EQ(w.c.sm.state, SIMREF_ST_LOCKED);
    CHECK_EQ(w.controlword_0f_writes, 0);
    CHECK_EQ(w.c.drive.enable_sequences, 0);
    CHECK_EQ(w.d.target[0], 0);
}

static void estop_and_reset_sequence(void)
{
    th_world_t w;
    th_world_init(&w, SIMREF_CAUSE_POWER_ON);
    th_world_arm_to_run(&w);
    th_world_cmd(&w, 0.2, 0);
    th_world_run(&w, 40000);
    CHECK(w.d.target[0] != 0);
    /* E-stop: Y32 low at once, spare contact active, contactor opens */
    th_world_pins(&w, 0, 1, 0, 0, 0);
    th_world_run(&w, 30000);
    CHECK_EQ(w.c.sm.state, SIMREF_ST_LOCKED);
    CHECK_EQ(w.d.target[0], 0);
    CHECK(w.d.sw_state != 0x27); /* disabled in LOCKED */
    CHECK(reason_logged(&w.c, SIMREF_R_HW_SAFETY_NOT_ENABLED));
    CHECK(reason_logged(&w.c, SIMREF_R_ESTOP_DIAG));
    /* RESET while the E-stop is still pressed: nothing */
    th_world_press_reset(&w, 300);
    CHECK_EQ(w.c.sm.state, SIMREF_ST_LOCKED);
    /* release the E-stop; still no motion until RESET */
    th_world_pins(&w, 1, 0, 0, 0, 1);
    for (int i = 0; i < 10; i++) {
        th_world_cmd(&w, 0.2, 0);
        th_world_run(&w, 50000);
    }
    CHECK_EQ(w.c.sm.state, SIMREF_ST_LOCKED);
    CHECK_EQ(w.d.target[0], 0);
    th_world_press_reset(&w, 400);
    CHECK_EQ(w.c.sm.state, SIMREF_ST_RUN);
    th_world_cmd(&w, 0.2, 0);
    th_world_run(&w, 40000);
    CHECK(w.d.target[0] != 0);
}

static void contactor_boundary(void)
{
    th_world_t w;
    th_world_init(&w, SIMREF_CAUSE_POWER_ON);
    th_world_arm_to_run(&w);
    th_world_pin(&w, SIMREF_PIN_CONTACTOR_FB, 0); /* feedback open while commanded closed */
    th_world_run(&w, (SIMREF_CONTACTOR_SETTLE_MS - 1) * 1000);
    CHECK_EQ(w.c.sm.state, SIMREF_ST_RUN);
    th_world_run(&w, 2000);
    CHECK_EQ(w.c.sm.state, SIMREF_ST_LOCKED);
    CHECK_EQ(w.c.input_detail, SIMREF_INPUT_DETAIL_CONTACTOR_FEEDBACK);
    CHECK(reason_logged(&w.c, SIMREF_R_INPUT_INVALID_IMPLAUSIBLE));
    CHECK_EQ(w.c.sm.first_reason, SIMREF_R_INPUT_INVALID_IMPLAUSIBLE);
}

static void reset_long_presses(void)
{
    th_world_t w;
    th_world_init(&w, SIMREF_CAUSE_POWER_ON);
    th_world_arm_to_run(&w);
    th_world_pin(&w, SIMREF_PIN_RESET, 1);
    for (int i = 0; i < 39; i++) {
        th_world_host_hb(&w);
        th_world_run(&w, 50000);
    }
    th_world_run(&w, 49000); /* 1999 ms + debounce */
    CHECK_EQ(w.c.sm.state, SIMREF_ST_RUN);
    th_world_host_hb(&w);
    th_world_run(&w, 1000 + SIMREF_INPUT_DEBOUNCE_MS * 1000);
    CHECK_EQ(w.c.sm.state, SIMREF_ST_LOCKED);
    CHECK(reason_logged(&w.c, SIMREF_R_INPUT_INVALID_RESET_HIGH));
    for (int i = 0; i < 62; i++) {
        th_world_host_hb(&w);
        th_world_run(&w, 50000);
    }
    CHECK(w.c.sm.reboot_pending);
    CHECK(reason_logged(&w.c, SIMREF_R_REBOOT_REQUESTED));
    th_world_pin(&w, SIMREF_PIN_RESET, 0);
    th_world_run(&w, 50000);
    CHECK_EQ(w.c.reboots, 1);
    CHECK_EQ(w.c.sm.state, SIMREF_ST_LOCKED);
    CHECK(w.c.sm.latches.jetson_stop);
    CHECK(reason_logged(&w.c, SIMREF_R_BOOT_SOFTWARE));
}

static void two_faults_one_tick(void)
{
    th_world_t w;
    th_world_init(&w, SIMREF_CAUSE_POWER_ON);
    th_world_arm_to_run(&w);
    /* Y32 low and the 5 s hold reached in the same step: reset_hold_5s first (priority) */
    th_world_pin(&w, SIMREF_PIN_RESET, 1);
    for (int i = 0; i < 99; i++) {
        th_world_host_hb(&w);
        th_world_run(&w, 50000);
    }
    /* now at 4950 ms of hold */
    th_world_run(&w, 49000 + SIMREF_INPUT_DEBOUNCE_MS * 1000);
    th_world_pin(&w, SIMREF_PIN_Y32, 0);
    th_world_run(&w, 1000);
    CHECK(reason_logged(&w.c, SIMREF_R_REBOOT_REQUESTED));
}

static void power_restore_no_auto_enable(void)
{
    th_world_t w;
    th_world_init(&w, SIMREF_CAUSE_POWER_ON);
    th_world_arm_to_run(&w);
    th_world_cmd(&w, 0.2, 0);
    th_world_run(&w, 40000);
    /* power cut: controller and drive restart */
    th_drive_power_cycle(&w);
    simref_ctrl_cfg_t cfg = w.c.cfg;
    simref_ctrl_init(&w.c, &cfg, w.t, w.c.send, w.c.send_ctx);
    th_world_pins(&w, 1, 0, 0, 0, 1);
    simref_ctrl_host_session(&w.c, 8, w.t);
    for (int i = 0; i < 40; i++) {
        simref_ctrl_host_heartbeat(&w.c, 8, w.t);
        simref_ctrl_host_cmd(&w.c, 8, (uint32_t)(100 + i), 0.2, 0, w.t);
        th_world_run(&w, 50000);
    }
    CHECK_EQ(w.c.sm.state, SIMREF_ST_LOCKED);
    CHECK(w.d.sw_state != 0x27);
    CHECK_EQ(w.d.target[0], 0);
    CHECK_EQ(w.c.sm.first_reason, SIMREF_R_BOOT_POWER_ON);
}

int main(void)
{
    RUN(arms_to_run);
    RUN(no_enable_without_reset);
    RUN(estop_and_reset_sequence);
    RUN(contactor_boundary);
    RUN(reset_long_presses);
    RUN(two_faults_one_tick);
    RUN(power_restore_no_auto_enable);
    return th_report("test_ctrl");
}
