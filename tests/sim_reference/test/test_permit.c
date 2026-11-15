/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
#include <string.h>

#include "simref/simref_permit.h"
#include "tharness.h"

static simref_permit_cfg_t cfg(void)
{
    simref_permit_cfg_t c = {.set = SIMREF_PP_ALL, .stop_v_max = 0.01, .stop_w_max = 0.02, .track_m = 0.5,
                             .stop_hold_us = 500000, .wheel_stale_us = 100000, .arm_stale_us = 200000,
                             .approved_poses = 1u << SIMREF_POSE_STOWED};
    return c;
}

static void feed(simref_permit_t *p, int64_t t, int64_t seq, double vl, double vr)
{
    simref_arm_t a = {SIMREF_POSE_STOWED, false, false};
    simref_permit_wheel(p, t, SIMREF_SRC_LEFT, 1, seq, true, vl);
    simref_permit_wheel(p, t, SIMREF_SRC_RIGHT, 1, seq, true, vr);
    simref_permit_arms(p, t, 2, seq, true, &a, &a, SIMREF_POSE_STOWED);
}

static void setup(simref_permit_t *p)
{
    simref_permit_cfg_t c = cfg();
    simref_permit_init(p, &c);
    simref_permit_source_session(p, SIMREF_SRC_LEFT, 1);
    simref_permit_source_session(p, SIMREF_SRC_RIGHT, 1);
    simref_permit_source_session(p, SIMREF_SRC_ARMS, 2);
    simref_permit_set_ctl017_link(p, true);
}

static void hold_boundary(void)
{
    simref_permit_t p;
    setup(&p);
    int64_t t = 0;
    for (int i = 1; i <= 11; i++, t += 50000) {
        feed(&p, t, i, 0.0, 0.0);
        simref_permit_evaluate(&p, t);
        if (t < 500000)
            CHECK_EQ(p.reason, SIMREF_PR_BASE_STOP_NOT_QUALIFIED);
    }
    /* last evaluation at t = 500 ms after the first stopped sample: exactly the hold time */
    CHECK(p.permit);
    feed(&p, t, 12, 0.02, 0.02);
    simref_permit_evaluate(&p, t);
    CHECK(!p.permit);
    CHECK(p.stop_request);
    CHECK_EQ(p.reason, SIMREF_PR_BASE_MOVING);
}

static void not_configured(void)
{
    simref_permit_cfg_t c = cfg();
    c.set &= ~(uint32_t)SIMREF_PP_ARM_STALE;
    simref_permit_t p;
    simref_permit_init(&p, &c);
    simref_permit_evaluate(&p, 0);
    CHECK(!p.permit);
    CHECK_EQ(p.reason, SIMREF_PR_NOT_CONFIGURED);
}

static void sequence_rules(void)
{
    simref_permit_t p;
    setup(&p);
    simref_permit_begin_step(&p);
    simref_permit_wheel(&p, 0, SIMREF_SRC_LEFT, 1, 5, true, 0);
    simref_permit_wheel(&p, 0, SIMREF_SRC_LEFT, 1, 5, true, 0);
    simref_permit_wheel(&p, 0, SIMREF_SRC_LEFT, 1, 4, true, 0);
    simref_permit_wheel(&p, 0, SIMREF_SRC_LEFT, 9, 6, true, 0);
    CHECK_EQ(p.n_rej, 3);
    CHECK_EQ(p.rej[0].why, SIMREF_REJ_DUPLICATE);
    CHECK_EQ(p.rej[1].why, SIMREF_REJ_OUT_OF_ORDER);
    CHECK_EQ(p.rej[2].why, SIMREF_REJ_WRONG_SESSION);
}

static void counter_wrap(void)
{
    simref_permit_tx_t tx = {.boot_id = 0xBEEF, .seq = 0xFFFE};
    uint8_t b[8];
    CHECK_EQ(simref_permit_encode(&tx, true, 0, b), 6);
    CHECK_EQ(tx.seq, 0xFFFF);
    CHECK_EQ(b[3] | (b[4] << 8), 0xFFFF);
    simref_permit_encode(&tx, true, 0, b);
    CHECK_EQ(tx.seq, 0);
    CHECK_EQ(b[3], 0);
    CHECK_EQ(b[4], 0);
    CHECK_EQ(b[0], SIMREF_PERMIT_VALUE);
    CHECK_EQ(b[1] | (b[2] << 8), 0xBEEF);
    simref_permit_encode(&tx, false, 7, b);
    CHECK_EQ(tx.seq, 1);
    CHECK_EQ(b[0], 0);
    CHECK_EQ(b[5], 7);
}

static void arms_and_link(void)
{
    simref_permit_t p;
    setup(&p);
    int64_t t = 0;
    for (int i = 1; i <= 12; i++, t += 50000) {
        feed(&p, t, i, 0, 0);
        simref_permit_evaluate(&p, t);
    }
    CHECK(p.permit);
    simref_permit_set_ctl017_link(&p, false);
    simref_permit_evaluate(&p, t);
    CHECK_EQ(p.reason, SIMREF_PR_CTL017_LINK_LOST);
    simref_permit_set_ctl017_link(&p, true);
    simref_permit_set_base_fault(&p, true);
    simref_permit_evaluate(&p, t);
    CHECK_EQ(p.reason, SIMREF_PR_BASE_FAULT);
    simref_permit_set_base_fault(&p, false);
    simref_arm_t carry = {SIMREF_POSE_CARRY, false, false}, ok = {SIMREF_POSE_STOWED, false, false};
    simref_permit_arms(&p, t, 2, 100, true, &ok, &carry, SIMREF_POSE_STOWED);
    simref_permit_evaluate(&p, t);
    CHECK_EQ(p.reason, SIMREF_PR_ARM_POSE_UNSAFE);
    simref_permit_evaluate(&p, t + 300000);
    CHECK(!p.permit);
}

int main(void)
{
    RUN(hold_boundary);
    RUN(not_configured);
    RUN(sequence_rules);
    RUN(counter_wrap);
    RUN(arms_and_link);
    return th_report("test_permit");
}
