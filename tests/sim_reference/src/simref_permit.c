/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
#include "simref/simref_permit.h"

#include <math.h>
#include <string.h>

static const char *const PR_NAMES[SIMREF_PR_COUNT] = {
    "OK", "BOOT", "NOT_CONFIGURED", "BASE_FEEDBACK_INVALID", "BASE_MOVING", "BASE_STOP_NOT_QUALIFIED",
    "ARM_STATUS_MISSING", "ARM_STATUS_STALE", "ARM_CONTROLLER_FAULT", "ARM_MOTION_ACTIVE",
    "ARM_POSE_UNSAFE", "BASE_FAULT", "CTL017_LINK_LOST"};
static const char *const REJ_NAMES[] = {"NONE", "WRONG_SESSION", "INVALID", "DUPLICATE", "OUT_OF_ORDER"};

const char *simref_permit_reason_name(simref_permit_reason_t r)
{
    return (unsigned)r < SIMREF_PR_COUNT ? PR_NAMES[r] : "?";
}
const char *simref_reject_name(simref_reject_t r) { return (unsigned)r <= SIMREF_REJ_OUT_OF_ORDER ? REJ_NAMES[r] : "?"; }

void simref_permit_init(simref_permit_t *p, const simref_permit_cfg_t *cfg)
{
    memset(p, 0, sizeof *p);
    if (cfg != NULL)
        p->cfg = *cfg;
    p->configured = (p->cfg.set & SIMREF_PP_ALL) == SIMREF_PP_ALL && p->cfg.track_m != 0.0;
    simref_permit_boot(p);
}

void simref_permit_boot(simref_permit_t *p)
{
    memset(p->s, 0, sizeof p->s);
    p->permit = p->stop_request = p->base_fault = p->ctl_link_ok = false;
    p->has_stopped_since = false;
    p->reason = SIMREF_PR_BOOT;
    p->n_rej = 0;
}

void simref_permit_begin_step(simref_permit_t *p) { p->n_rej = 0; }

void simref_permit_source_session(simref_permit_t *p, simref_src_t src, uint32_t session)
{
    if ((unsigned)src >= SIMREF_SRC_COUNT)
        return;
    simref_source_t *s = &p->s[src];
    memset(s, 0, sizeof *s);
    s->has_session = true;
    s->session = session;
}

static simref_reject_t accept(simref_source_t *s, int64_t now, uint32_t session, int64_t seq, bool valid)
{
    if (!s->has_session || session != s->session)
        return SIMREF_REJ_WRONG_SESSION;
    if (!valid) {
        s->has_sample = false;
        return SIMREF_REJ_INVALID;
    }
    if (s->has_seq && seq == s->last_seq)
        return SIMREF_REJ_DUPLICATE;
    if (s->has_seq && seq < s->last_seq)
        return SIMREF_REJ_OUT_OF_ORDER;
    s->has_seq = true;
    s->last_seq = seq;
    s->has_sample = true;
    s->rx_us = now;
    return SIMREF_REJ_NONE;
}

static void reject(simref_permit_t *p, simref_src_t src, simref_reject_t why)
{
    /* Fail closed (INTERPRETATION of #20, as the model): the source loses its sample. */
    p->s[src].has_sample = false;
    if (p->n_rej < SIMREF_PERMIT_MAX_REJ) {
        p->rej[p->n_rej].src = src;
        p->rej[p->n_rej].why = why;
        p->n_rej++;
    }
}

void simref_permit_wheel(simref_permit_t *p, int64_t now_us, simref_src_t side, uint32_t session, int64_t seq,
                         bool valid, double v)
{
    if (side != SIMREF_SRC_LEFT && side != SIMREF_SRC_RIGHT)
        return;
    simref_reject_t why = accept(&p->s[side], now_us, session, seq, valid);
    if (why != SIMREF_REJ_NONE) {
        reject(p, side, why);
        p->has_stopped_since = false; /* a wheel rejection restarts the stopped qualification */
        return;
    }
    p->s[side].v = v;
}

void simref_permit_arms(simref_permit_t *p, int64_t now_us, uint32_t session, int64_t seq, bool valid,
                        const simref_arm_t *left, const simref_arm_t *right, simref_pose_t bimanual)
{
    simref_source_t *s = &p->s[SIMREF_SRC_ARMS];
    simref_reject_t why = accept(s, now_us, session, seq, valid);
    if (why != SIMREF_REJ_NONE) {
        reject(p, SIMREF_SRC_ARMS, why);
        return;
    }
    s->left = *left;
    s->right = *right;
    s->bimanual = bimanual;
}

void simref_permit_set_base_fault(simref_permit_t *p, bool active) { p->base_fault = active; }
void simref_permit_set_ctl017_link(simref_permit_t *p, bool ok) { p->ctl_link_ok = ok; }

static bool fresh(const simref_source_t *s, int64_t now, int64_t stale_us)
{
    return s->has_sample && (now - s->rx_us) <= stale_us;
}

static bool pose_ok(const simref_permit_t *p, simref_pose_t pose)
{
    return (p->cfg.approved_poses & (1u << pose)) != 0;
}

void simref_permit_evaluate(simref_permit_t *p, int64_t now)
{
    bool was = p->permit;
    simref_permit_reason_t r = SIMREF_PR_OK;
    const simref_permit_cfg_t *c = &p->cfg;
    if (!p->configured)
        r = SIMREF_PR_NOT_CONFIGURED;
    bool wheels_ok = fresh(&p->s[SIMREF_SRC_LEFT], now, c->wheel_stale_us) &&
                     fresh(&p->s[SIMREF_SRC_RIGHT], now, c->wheel_stale_us);
    if (r == SIMREF_PR_OK && !wheels_ok) {
        r = SIMREF_PR_BASE_FEEDBACK_INVALID;
        p->has_stopped_since = false;
    }
    if (r == SIMREF_PR_OK) {
        double vl = p->s[SIMREF_SRC_LEFT].v, vr = p->s[SIMREF_SRC_RIGHT].v;
        double v = (vl + vr) / 2.0, w = (vr - vl) / c->track_m;
        if (fabs(v) >= c->stop_v_max || fabs(w) >= c->stop_w_max) {
            r = SIMREF_PR_BASE_MOVING;
            p->has_stopped_since = false;
        } else if (!p->has_stopped_since) {
            p->has_stopped_since = true;
            p->stopped_since_us = now;
        }
    }
    if (r == SIMREF_PR_OK && (now - p->stopped_since_us) < c->stop_hold_us)
        r = SIMREF_PR_BASE_STOP_NOT_QUALIFIED;
    if (r == SIMREF_PR_OK) {
        const simref_source_t *a = &p->s[SIMREF_SRC_ARMS];
        if (!a->has_sample)
            r = SIMREF_PR_ARM_STATUS_MISSING;
        else if (!fresh(a, now, c->arm_stale_us))
            r = SIMREF_PR_ARM_STATUS_STALE;
        else {
            const simref_arm_t *arms[2] = {&a->left, &a->right};
            for (int i = 0; i < 2 && r == SIMREF_PR_OK; i++) {
                if (arms[i]->fault)
                    r = SIMREF_PR_ARM_CONTROLLER_FAULT;
                else if (arms[i]->moving)
                    r = SIMREF_PR_ARM_MOTION_ACTIVE;
                else if (!pose_ok(p, arms[i]->pose))
                    r = SIMREF_PR_ARM_POSE_UNSAFE;
            }
            if (r == SIMREF_PR_OK && !pose_ok(p, a->bimanual))
                r = SIMREF_PR_ARM_POSE_UNSAFE;
        }
    }
    if (r == SIMREF_PR_OK && p->base_fault)
        r = SIMREF_PR_BASE_FAULT;
    if (r == SIMREF_PR_OK && !p->ctl_link_ok)
        r = SIMREF_PR_CTL017_LINK_LOST;
    p->permit = (r == SIMREF_PR_OK);
    p->reason = r;
    p->stop_request = was && !p->permit;
}

size_t simref_permit_encode(simref_permit_tx_t *tx, bool permit, uint8_t reason, uint8_t out[8])
{
    tx->seq = (uint16_t)(tx->seq + 1u);
    memset(out, 0, 8);
    out[0] = permit ? (uint8_t)SIMREF_PERMIT_VALUE : 0u;
    out[1] = (uint8_t)(tx->boot_id & 0xFFu);
    out[2] = (uint8_t)(tx->boot_id >> 8);
    out[3] = (uint8_t)(tx->seq & 0xFFu);
    out[4] = (uint8_t)(tx->seq >> 8);
    out[5] = reason;
    return 6;
}
