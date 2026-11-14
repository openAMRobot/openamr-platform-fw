/* SPDX-License-Identifier: MIT */
/*
 * TEST ORACLE, not production code. A small reporting model that turns the
 * module outputs into I8-shaped subsystem states, a fault list and a
 * controller health summary, so the host gate of platform-sw #137 has
 * something to judge. The owner's firmware decides these things; this model
 * only makes the integration proof runnable and is compared with the
 * owner's code later. It decides nothing about motion: the operating state
 * in the record is a scenario input.
 *
 * Fault codes are the BaseFault constants of interfaces #38 (group 18001).
 * Where #38 has no code for a condition, the subsystem carries
 * OAMR_SIL_NO_CODE and the JSON shows null; see the mismatch table in
 * docs/architecture/readonly-chain.md.
 */
#include <string.h>

#include "chain.h"

#define FC_DRIVER_COMM_LOST 18003u
#define FC_DRIVER_FAULT 18004u
#define FC_DRIVER_FEEDBACK_INVALID 18005u
#define FC_IMU_FAULT 18007u
#define FC_BMS_COMM_LOST 18008u

#define LIFECYCLE_ACTIVE 1u
#define LIFECYCLE_LATCHED 2u
#define OBS_NO 1u
#define OBS_YES 2u

#define STATE_UNKNOWN 0u
#define STATE_INITIALIZING 1u
#define STATE_READY 2u
#define STATE_DEGRADED 3u
#define STATE_FAULTED 4u

static uint8_t fresh_to_ss(oamr_fresh_state_t f)
{
    switch (f) {
    case OAMR_FRESH_FRESH:
        return SS_OK;
    case OAMR_FRESH_STALE:
        return SS_STALE;
    case OAMR_FRESH_NEVER:
    case OAMR_FRESH_UNKNOWN:
    default:
        return SS_UNKNOWN;
    }
}

static void fill_age(oamr_sil_subsystem_t *s, oamr_fresh_t *f, uint32_t now32)
{
    uint32_t age = 0u;
    s->age_known = oamr_fresh_age(f, now32, &age);
    s->data_age_us = s->age_known ? age : 0u;
}

void oamr_sil_model_subsystems(oamr_sil_chain_t *c, oamr_sil_subsystem_t subs[SUB_COUNT])
{
    const uint32_t now32 = (uint32_t)c->now_us;
    memset(subs, 0, sizeof(oamr_sil_subsystem_t) * SUB_COUNT);
    for (unsigned i = 0; i < SUB_COUNT; i++) {
        subs[i].id = (uint8_t)(i + 1u);
        subs[i].detail = "";
    }

    /* Motor driver: heartbeat consumer plus statusword and fault code. */
    oamr_sil_subsystem_t *d = &subs[SUB_MOTOR_DRIVER - 1];
    const oamr_co_hb_class_t hb = c->hb.last_class;
    if (hb == OAMR_CO_HB_LOST) {
        d->state = SS_STALE;
        d->fault_code = FC_DRIVER_COMM_LOST;
        d->detail = "heartbeat_lost";
    } else if (c->drive_restarted) {
        d->state = SS_DEGRADED;
        d->fault_code = FC_DRIVER_COMM_LOST;
        d->detail = "drive_restarted";
    } else if (hb != OAMR_CO_HB_ALIVE) {
        d->state = SS_UNKNOWN;
        d->detail = "heartbeat_waiting";
    } else if (c->sw.meta.validity != OAMR_TLM_VALID || c->fault.meta.validity != OAMR_TLM_VALID) {
        d->state = (c->sw.meta.validity == OAMR_TLM_NOT_READ) ? SS_UNKNOWN : SS_STALE;
        d->fault_code = d->state == SS_UNKNOWN ? 0u : FC_DRIVER_FEEDBACK_INVALID;
        d->detail = oamr_tlm_validity_name(c->sw.meta.validity != OAMR_TLM_VALID ? c->sw.meta.validity
                                                                                 : c->fault.meta.validity);
    } else if (c->fault.any_fault || c->sw.fault) {
        d->state = SS_DEGRADED;
        d->fault_code = FC_DRIVER_FAULT;
        d->detail = "drive_reports_fault";
    } else {
        d->state = SS_OK;
    }
    if (hb == OAMR_CO_HB_ALIVE || hb == OAMR_CO_HB_LOST) {
        d->age_known = c->hb.seen && c->now_us >= c->hb.last_seen_us;
        d->data_age_us = d->age_known ? (uint32_t)(c->now_us - c->hb.last_seen_us) : 0u;
    }

    /* Encoders: both wheel samples, each on its own freshness tracker. */
    oamr_sil_subsystem_t *e = &subs[SUB_ENCODERS - 1];
    const uint8_t w1 = fresh_to_ss(oamr_fresh_classify(&c->wheel_fresh[0], now32));
    const uint8_t w2 = fresh_to_ss(oamr_fresh_classify(&c->wheel_fresh[1], now32));
    uint32_t a1 = 0u, a2 = 0u;
    const bool k1 = oamr_fresh_age(&c->wheel_fresh[0], now32, &a1);
    const bool k2 = oamr_fresh_age(&c->wheel_fresh[1], now32, &a2);
    e->age_known = k1 && k2;
    e->data_age_us = e->age_known ? (a1 > a2 ? a1 : a2) : 0u;
    if (w1 == SS_OK && w2 == SS_OK) {
        if (c->resolution.meta.validity != OAMR_TLM_VALID || !c->resolution.confirmed) {
            e->state = SS_DEGRADED;
            e->fault_code = FC_DRIVER_FEEDBACK_INVALID;
            e->detail = "speed_resolution_unconfirmed";
        } else {
            e->state = SS_OK;
        }
    } else if (w1 == SS_UNKNOWN && w2 == SS_UNKNOWN) {
        e->state = SS_UNKNOWN;
        e->detail = "no_wheel_sample";
    } else {
        e->state = SS_STALE;
        e->fault_code = FC_DRIVER_FEEDBACK_INVALID;
        e->detail = (w1 == SS_OK) ? "channel2_not_fresh" : (w2 == SS_OK ? "channel1_not_fresh" : "both_not_fresh");
    }

    /* IMU. */
    oamr_sil_subsystem_t *m = &subs[SUB_IMU - 1];
    if (c->icm_init == OAMR_ICM_WRONG_ID) {
        m->state = SS_DEGRADED;
        m->fault_code = FC_IMU_FAULT;
        m->detail = "wrong_who_am_i";
    } else if (c->icm_init != OAMR_ICM_OK) {
        m->state = SS_ABSENT;
        m->fault_code = FC_IMU_FAULT;
        m->detail = "init_failed";
    } else {
        m->state = fresh_to_ss(oamr_fresh_classify(&c->imu_fresh, now32));
        m->fault_code = m->state == SS_STALE ? FC_IMU_FAULT : 0u;
        fill_age(m, &c->imu_fresh, now32);
    }

    /* BMS: decoder present, protocol unconfirmed, so never anything but UNKNOWN. */
    oamr_sil_subsystem_t *b = &subs[SUB_BMS - 1];
    b->state = SS_UNKNOWN;
    b->detail = oamr_daly_rx_usable(&c->daly) ? "usable" : "protocol_unconfirmed";

    /* Ultrasonic range, four units. */
    for (unsigned i = 0; i < OAMR_SIL_N_MB; i++) {
        oamr_sil_subsystem_t *r = &subs[SUB_RANGE_FL - 1 + i];
        r->state = fresh_to_ss(oamr_fresh_classify(&c->mb_fresh[i], now32));
        fill_age(r, &c->mb_fresh[i], now32);
        if (r->state == SS_OK && c->mb_last[i] == OAMR_MB_FAULT) {
            r->state = SS_DEGRADED;
            r->detail = "reading_zero";
        } else if (r->state == SS_OK && c->mb_last[i] == OAMR_MB_NO_TARGET) {
            r->detail = "no_target";
        } else if (r->state == SS_OK && c->mb_last[i] == OAMR_MB_AT_MIN) {
            r->detail = "at_min";
        }
    }

    /* Cliff ToF, two units. NO_RETURN on a downward cliff sensor is not
     * "floor present": DEGRADED, never OK. */
    for (unsigned i = 0; i < OAMR_SIL_N_TFL; i++) {
        oamr_sil_subsystem_t *t = &subs[SUB_CLIFF_FRONT - 1 + i];
        t->state = fresh_to_ss(oamr_fresh_classify(&c->tfl_fresh[i], now32));
        fill_age(t, &c->tfl_fresh[i], now32);
        if (t->state == SS_OK && c->tfl_last[i] != OAMR_TFL_OK) {
            t->state = SS_DEGRADED;
            t->detail = "reading_not_usable";
        }
    }
}

static void raise_fault(oamr_sil_chain_t *c, uint16_t code, bool latch)
{
    for (unsigned i = 0; i < c->n_faults; i++) {
        if (c->faults[i].code == code) {
            c->faults[i].condition_active = OBS_YES;
            if (latch) {
                c->faults[i].lifecycle = LIFECYCLE_LATCHED;
            }
            return;
        }
    }
    if (c->n_faults < OAMR_SIL_MAX_FAULTS) {
        c->faults[c->n_faults++] =
            (oamr_sil_fault_t){code, latch ? LIFECYCLE_LATCHED : LIFECYCLE_ACTIVE, OBS_YES, latch};
    }
}

void oamr_sil_model_update_faults(oamr_sil_chain_t *c, const oamr_sil_subsystem_t subs[SUB_COUNT])
{
    /* Conditions are re-evaluated each record; latched entries stay until
     * the controller restarts (no reset service in this proof). */
    for (unsigned i = 0; i < c->n_faults; i++) {
        c->faults[i].condition_active = OBS_NO;
    }
    for (unsigned i = 0; i < SUB_COUNT; i++) {
        const uint16_t code = subs[i].fault_code;
        if (code == 0u) {
            continue;
        }
        /* Drive comm loss and a drive restart latch; feedback and IMU do not. */
        raise_fault(c, code, code == FC_DRIVER_COMM_LOST || code == FC_DRIVER_FAULT);
    }
    /* Drop non-latched entries whose condition is gone. */
    uint8_t n = 0u;
    for (unsigned i = 0; i < c->n_faults; i++) {
        if (c->faults[i].lifecycle == LIFECYCLE_LATCHED || c->faults[i].condition_active == OBS_YES) {
            c->faults[n++] = c->faults[i];
        }
    }
    c->n_faults = n;
    (void)FC_BMS_COMM_LOST;
}

uint8_t oamr_sil_model_controller_state(const oamr_sil_chain_t *c,
                                        const oamr_sil_subsystem_t subs[SUB_COUNT])
{
    if (c->n_faults > 0u) {
        return STATE_FAULTED;
    }
    bool unknown = false;
    bool not_ok = false;
    for (unsigned i = 0; i < SUB_COUNT; i++) {
        if (subs[i].id == SUB_BMS && c->val[K_BMS_REQUIRED] == 0.0) {
            continue; /* TODO(Evans): BMS required for READY once the protocol is confirmed */
        }
        if (subs[i].state == SS_UNKNOWN) {
            unknown = true;
        } else if (subs[i].state != SS_OK) {
            not_ok = true;
        }
    }
    if (not_ok) {
        return STATE_DEGRADED;
    }
    if (unknown) {
        return STATE_INITIALIZING;
    }
    return STATE_READY;
}
