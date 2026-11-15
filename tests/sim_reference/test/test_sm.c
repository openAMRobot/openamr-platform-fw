/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
#include <string.h>

#include "simref/simref_sm.h"
#include "tharness.h"

static simref_sm_inputs_t all_ok(void)
{
    return (simref_sm_inputs_t){.run_ok = true, .base_stopped = true, .arming_ok = true, .qualified = true, .charge_pre_ok = true};
}

static simref_sm_err_t one(simref_sm_t *sm, simref_event_id_t id)
{
    simref_event_t e = {.id = id};
    simref_sm_inputs_t in = all_ok();
    return simref_sm_tick(sm, &in, &e, 1);
}

static void to_run(simref_sm_t *sm)
{
    simref_sm_init(sm);
    one(sm, SIMREF_EV_BOOT);
    one(sm, SIMREF_EV_JETSON_STOP_RELEASE);
    one(sm, SIMREF_EV_RESET_SHORT);
}

static void boot_and_reset_sequence(void)
{
    simref_sm_t sm;
    simref_sm_init(&sm);
    CHECK_EQ(one(&sm, SIMREF_EV_BOOT), SIMREF_SM_OK);
    CHECK_EQ(sm.state, SIMREF_ST_LOCKED);
    CHECK(sm.latches.jetson_stop);
    CHECK_EQ(sm.reasons[0], SIMREF_R_BOOT_POWER_ON);
    /* RESET before the Jetson Stop release does not enter RUN (L1 note, Q2) */
    CHECK_EQ(one(&sm, SIMREF_EV_RESET_SHORT), SIMREF_SM_OK);
    CHECK_EQ(sm.state, SIMREF_ST_LOCKED);
    CHECK(sm.latches.reset_seen);
    one(&sm, SIMREF_EV_JETSON_STOP_RELEASE);
    CHECK_EQ(sm.state, SIMREF_ST_LOCKED);
    one(&sm, SIMREF_EV_RESET_SHORT);
    CHECK_EQ(sm.state, SIMREF_ST_RUN);
    CHECK(!sm.motor_follow);
    CHECK(simref_sm_motion_admissible(&sm));
    CHECK_EQ(sm.n_mask, 0);
}

static void reset_not_qualified(void)
{
    simref_sm_t sm;
    simref_sm_init(&sm);
    one(&sm, SIMREF_EV_BOOT);
    one(&sm, SIMREF_EV_JETSON_STOP_RELEASE);
    simref_sm_inputs_t in = all_ok();
    in.qualified = false;
    simref_event_t e = {.id = SIMREF_EV_RESET_SHORT};
    CHECK_EQ(simref_sm_tick(&sm, &in, &e, 1), SIMREF_SM_OK);
    CHECK_EQ(sm.state, SIMREF_ST_LOCKED);
    CHECK_EQ(sm.reasons[0], SIMREF_R_QUALIFICATION_FAILED);
    in.qualified = true;
    in.run_ok = false;
    CHECK_EQ(simref_sm_tick(&sm, &in, &e, 1), SIMREF_SM_OK);
    CHECK_EQ(sm.state, SIMREF_ST_LOCKED); /* run_ok false: Q2 */
}

static void simultaneous_faults_priority(void)
{
    /* CHARGING, one tick: charge_interlock listed first, wcm_fault second: W1 then K2 (rank) */
    simref_sm_t sm;
    simref_sm_init(&sm);
    one(&sm, SIMREF_EV_BOOT);
    one(&sm, SIMREF_EV_JETSON_STOP_RELEASE);
    simref_event_t cc = {.id = SIMREF_EV_CHARGE_CMD};
    simref_sm_inputs_t in = all_ok();
    simref_sm_tick(&sm, &in, &cc, 1);
    CHECK_EQ(sm.state, SIMREF_ST_CHARGING);
    simref_event_t two[2] = {{.id = SIMREF_EV_CHARGE_INTERLOCK}, {.id = SIMREF_EV_WCM_FAULT}};
    CHECK_EQ(simref_sm_tick(&sm, &in, two, 2), SIMREF_SM_OK);
    CHECK_EQ(sm.state, SIMREF_ST_CHARGER_FAULT);
    CHECK(sm.latches.wcm_latched && sm.latches.interlock_latched);
    CHECK_EQ(sm.reasons[0], SIMREF_R_CHARGER_FAULT);
    CHECK_EQ(sm.reasons[1], SIMREF_R_CHARGE_INTERLOCK);
    CHECK_EQ(sm.first_reason, SIMREF_R_CHARGER_FAULT);
    /* RUN, one tick: E-stop diagnostic and the 5 s hold: R1 first */
    to_run(&sm);
    simref_event_t two2[2] = {{.id = SIMREF_EV_ESTOP_DIAG}, {.id = SIMREF_EV_RESET_HOLD_5S}};
    CHECK_EQ(simref_sm_tick(&sm, &in, two2, 2), SIMREF_SM_OK);
    CHECK_EQ(sm.reasons[0], SIMREF_R_REBOOT_REQUESTED);
    CHECK_EQ(sm.reasons[1], SIMREF_R_ESTOP_DIAG);
    CHECK_EQ(sm.first_reason, SIMREF_R_REBOOT_REQUESTED);
    CHECK(sm.reboot_pending);
    /* RUN, others keep arrival order: estop then session_lost -> first_reason ESTOP_DIAG */
    to_run(&sm);
    simref_event_t two3[2] = {{.id = SIMREF_EV_ESTOP_DIAG}, {.id = SIMREF_EV_SESSION_LOST}};
    simref_sm_tick(&sm, &in, two3, 2);
    CHECK_EQ(sm.first_reason, SIMREF_R_ESTOP_DIAG);
    CHECK_EQ(sm.n_mask, 2);
}

static void spec_errors(void)
{
    simref_sm_t sm;
    simref_sm_init(&sm);
    one(&sm, SIMREF_EV_BOOT);
    CHECK_EQ(one(&sm, SIMREF_EV_CMD_STALE), SIMREF_SM_E_NOT_APPLICABLE);
    CHECK_EQ(one(&sm, SIMREF_EV_CHARGE_INTERLOCK), SIMREF_SM_E_NOT_APPLICABLE);
    simref_event_t many[SIMREF_SM_MAX_TICK_EVENTS + 1];
    memset(many, 0, sizeof many);
    CHECK_EQ(simref_sm_tick(&sm, NULL, many, SIMREF_SM_MAX_TICK_EVENTS + 1), SIMREF_SM_E_ARG);
}

static void session_filters(void)
{
    simref_sm_t sm;
    to_run(&sm);
    simref_event_t e = {.id = SIMREF_EV_MOTION_CMD, .session_old = true, .seq_replay = true};
    simref_sm_inputs_t in = all_ok();
    simref_sm_tick(&sm, &in, &e, 1);
    CHECK_EQ(sm.reasons[0], SIMREF_R_SESSION_OLD); /* S1 before S2 (F3) */
    CHECK(!sm.motor_follow);
    e.session_old = false;
    simref_sm_tick(&sm, &in, &e, 1);
    CHECK_EQ(sm.reasons[0], SIMREF_R_SEQ_REJECTED);
    e.seq_replay = false;
    simref_sm_tick(&sm, &in, &e, 1);
    CHECK(sm.motor_follow);
    one(&sm, SIMREF_EV_CMD_STALE);
    CHECK(!sm.motor_follow);
    CHECK_EQ(sm.state, SIMREF_ST_RUN); /* non-latching */
}

static void reset_after_fault(void)
{
    simref_sm_t sm;
    to_run(&sm);
    one(&sm, SIMREF_EV_SAFETY_OK_LOW);
    CHECK_EQ(sm.state, SIMREF_ST_LOCKED);
    CHECK(!sm.latches.reset_seen);
    CHECK(!simref_sm_motion_admissible(&sm));
    one(&sm, SIMREF_EV_RESET_SHORT);
    CHECK_EQ(sm.state, SIMREF_ST_RUN);
}

int main(void)
{
    RUN(boot_and_reset_sequence);
    RUN(reset_not_qualified);
    RUN(simultaneous_faults_priority);
    RUN(spec_errors);
    RUN(session_filters);
    RUN(reset_after_fault);
    return th_report("test_sm");
}
