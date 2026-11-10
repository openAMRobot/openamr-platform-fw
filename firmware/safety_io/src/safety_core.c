/* SPDX-License-Identifier: MIT */
/*
 * E-01 safety state machine rev 3. PROPOSAL for #17 (Evans to adopt or
 * rewrite); see safety_core.h for the rules and the interpretations made.
 */
#include "oamr/safety_core.h"

#include <math.h>
#include <string.h>

void oamr_sc_default_config(oamr_sc_config_t *cfg)
{
    cfg->debounce_us = 20000u;
    cfg->short_press_max_us = 2000000u;
    cfg->long_press_us = 5000000u;
    cfg->reset_window_us = 1000000u;
    cfg->controller_reset_delay_us = 2000000u;
    cfg->charge_interlock_us = 500000u;
    cfg->comms_timeout_us = 500000u;
    cfg->bms_stale_us = 3000000u;          /* TODO(Evans) */
    cfg->column_estop_stale_us = 500000u;  /* TODO(Evans) */
    cfg->encoder_stale_us = 100000u;       /* TODO(Evans) */
    cfg->stopped_rpm = 1.0f;
    cfg->stopped_hold_us = 500000u;
}

/* Debounced inputs start in their unsafe value. */
static void debounce_init(oamr_sc_debounce_t *d, bool unsafe_value)
{
    memset(d, 0, sizeof(*d));
    d->value = unsafe_value;
}

void oamr_sc_init(oamr_sc_t *sc, const oamr_sc_config_t *cfg)
{
    memset(sc, 0, sizeof(*sc));
    sc->cfg = *cfg;
    debounce_init(&sc->safety_ok, false);
    debounce_init(&sc->estop_base, true);
    debounce_init(&sc->reset, true);  /* a press only counts after RESET was seen released */
    debounce_init(&sc->wcm, true);
    sc->prev_sw_stop = true;          /* software Stop is asserted at boot */
    sc->out.state = OAMR_SC_LOCKED;
    sc->out.reset_lamp = true;
}

/* Changes towards `unsafe` act at once (unless filter_both); changes away
 * from it must hold for the debounce time. Returns true if the value changed. */
static bool debounce(oamr_sc_debounce_t *d, bool raw, bool unsafe, bool filter_both,
                     uint64_t now_us, uint64_t hold_us)
{
    if (raw == d->value) {
        d->pending = false;
        return false;
    }
    if (raw == unsafe && !filter_both) {
        d->value = raw;
        d->pending = false;
        d->changed_us = now_us;
        return true;
    }
    if (!d->pending) {
        d->pending = true;
        d->pending_since_us = now_us;
    }
    if (now_us - d->pending_since_us >= hold_us) {
        d->value = raw;
        d->pending = false;
        d->changed_us = d->pending_since_us;
        return true;
    }
    return false;
}

static bool fresh(bool seen, uint64_t rx_us, uint64_t now_us, uint64_t limit_us)
{
    return seen && now_us >= rx_us && (now_us - rx_us) < limit_us;
}

/* Same-tick ordering for exit reasons: hardware safety, then charger fault,
 * then software Stop, then the rest. */
static const uint32_t k_reason_order[] = {
    OAMR_SC_R_REBOOT_REQUESTED,
    OAMR_SC_R_ESTOP_DIAG,
    OAMR_SC_R_HW_SAFETY_NOT_ENABLED,
    OAMR_SC_R_CHARGER_FAULT,
    OAMR_SC_R_CHARGE_INTERLOCK,
    OAMR_SC_R_SOFTWARE_STOP,
    OAMR_SC_R_BMS_FAULT,
    OAMR_SC_R_COMMS_LOST,
    OAMR_SC_R_INPUT_INVALID,
    OAMR_SC_R_PNOZ_DID_NOT_ENABLE,
    OAMR_SC_R_RESET_IN_PROGRESS,
};

static uint32_t top_reason(uint32_t reasons)
{
    for (size_t i = 0; i < sizeof(k_reason_order) / sizeof(k_reason_order[0]); i++) {
        if (reasons & k_reason_order[i]) {
            return k_reason_order[i];
        }
    }
    return 0u;
}

static void clear_reset_seen(oamr_sc_t *sc)
{
    sc->out.reset_seen = false;
    sc->out.reset_window_open = false;
}

static void enter(oamr_sc_t *sc, oamr_sc_state_t s, uint32_t reason)
{
    const oamr_sc_state_t from = sc->out.state;
    if (from == s) {
        return;
    }
    if (from == OAMR_SC_RUN || from == OAMR_SC_CHARGING) {
        sc->out.transition_reason = reason;
    }
    if (from == OAMR_SC_RUN && reason != 0u) {
        sc->out.first_reason = reason;
    }
    if (from == OAMR_SC_RUN) {
        clear_reset_seen(sc);
    }
    if (s == OAMR_SC_RUN) {
        sc->out.first_reason = 0u;
        sc->pnoz_did_not_enable = false;
    }
    if (s != OAMR_SC_CHARGER_FAULT) {
        sc->out.latches = 0u;
    }
    sc->out.state = s;
}

const oamr_sc_outputs_t *oamr_sc_step(oamr_sc_t *sc, const oamr_sc_inputs_t *in, uint64_t now_us)
{
    const oamr_sc_config_t *c = &sc->cfg;
    oamr_sc_outputs_t *o = &sc->out;

    /* ---- inputs: validity, debounce, freshness ---- */
    const bool gpio_valid = in->gpio_valid;
    if (gpio_valid) {
        sc->inputs_complete = true;
        debounce(&sc->safety_ok, in->safety_ok, false, false, now_us, c->debounce_us);
        debounce(&sc->estop_base, in->estop_base_active, true, false, now_us, c->debounce_us);
        debounce(&sc->reset, in->reset_pressed, true, true, now_us, c->debounce_us);
        debounce(&sc->wcm, in->wcm_fault, true, false, now_us, c->debounce_us);
    }
    const bool input_invalid = !gpio_valid || !sc->inputs_complete;
    /* An invalid sample never reads as safe. */
    const bool safety_ok = !input_invalid && sc->safety_ok.value;
    const bool estop_base = input_invalid || sc->estop_base.value;
    const bool wcm_fault = sc->wcm.value;

    const bool comms_ok = fresh(in->host_seen, in->host_rx_us, now_us, c->comms_timeout_us);
    const bool bms_ok = !in->bms_fault && fresh(in->bms_seen, in->bms_rx_us, now_us, c->bms_stale_us);
    const bool column_ok = in->estop_column == OAMR_SC_COLUMN_RELEASED &&
                           fresh(in->estop_column_seen, in->estop_column_rx_us, now_us,
                                 c->column_estop_stale_us);
    const bool estop_diag_bad = estop_base || !column_ok;
    const bool enc_fresh = fresh(in->encoder_seen, in->encoder_rx_us, now_us, c->encoder_stale_us);
    const bool below = enc_fresh && fabsf(in->encoder_left_rpm) < c->stopped_rpm &&
                       fabsf(in->encoder_right_rpm) < c->stopped_rpm &&
                       isfinite(in->encoder_left_rpm) && isfinite(in->encoder_right_rpm);
    if (below) {
        if (!sc->stopped_tracking) {
            sc->stopped_tracking = true;
            sc->stopped_since_us = now_us;
        }
    } else {
        sc->stopped_tracking = false;
    }
    const bool stopped = sc->stopped_tracking && (now_us - sc->stopped_since_us) >= c->stopped_hold_us;

    /* ---- edges ---- */
    const bool sw_stop_rise = in->sw_stop && !sc->prev_sw_stop;
    const bool safety_ok_fall = !safety_ok && sc->prev_safety_ok;
    const bool charge_edge = in->charge_cmd && !sc->prev_charge_cmd;
    sc->prev_sw_stop = in->sw_stop;
    sc->prev_safety_ok = safety_ok;
    sc->prev_charge_cmd = in->charge_cmd;

    /* ---- RESET press timing ---- */
    bool short_press = false;
    bool long_press = false;
    const bool reset_now = sc->reset.value;
    if (!reset_now) {
        sc->reset_released_seen = true;
    }
    if (reset_now && !sc->prev_reset && sc->reset_released_seen) {
        sc->press_active = true;
        sc->long_press_fired = false;
        sc->press_start_us = sc->reset.changed_us;
    }
    if (sc->press_active && !sc->long_press_fired) {
        const uint64_t end = (reset_now && sc->reset.pending) ? sc->reset.pending_since_us
                             : (reset_now ? now_us : sc->reset.changed_us);
        if (end - sc->press_start_us >= c->long_press_us) {
            sc->long_press_fired = true;
            long_press = true;
        }
    }
    if (!reset_now && sc->prev_reset && sc->press_active) {
        const uint64_t dur = sc->reset.changed_us - sc->press_start_us;
        if (!sc->long_press_fired && dur < c->short_press_max_us) {
            short_press = true;
        }
        sc->press_active = false;
    }
    sc->prev_reset = reset_now;

    /* ---- reset_seen clear conditions ---- */
    if (sw_stop_rise || safety_ok_fall || estop_diag_bad || !comms_ok || !bms_ok || input_invalid) {
        clear_reset_seen(sc);
    }

    /* ---- transitions, in priority order ---- */
    uint32_t exit_reasons = 0u;
    if (estop_diag_bad) exit_reasons |= OAMR_SC_R_ESTOP_DIAG;
    if (!safety_ok) exit_reasons |= OAMR_SC_R_HW_SAFETY_NOT_ENABLED;
    if (in->sw_stop) exit_reasons |= OAMR_SC_R_SOFTWARE_STOP;
    if (!bms_ok) exit_reasons |= OAMR_SC_R_BMS_FAULT;
    if (!comms_ok) exit_reasons |= OAMR_SC_R_COMMS_LOST;
    if (input_invalid) exit_reasons |= OAMR_SC_R_INPUT_INVALID;

    bool transitioned = false;
    /* 1. Five-second hold: any state to LOCKED, reboot request, controller reset later. */
    if (long_press) {
        enter(sc, OAMR_SC_LOCKED, OAMR_SC_R_REBOOT_REQUESTED);
        clear_reset_seen(sc);
        if (!o->reboot_request) {
            o->reboot_request = true;
            sc->controller_reset_at_us = now_us + c->controller_reset_delay_us;
        }
        transitioned = true;
    }

    /* 2. Latching faults while charging. */
    if (!transitioned && o->state == OAMR_SC_CHARGING) {
        uint32_t latch = 0u;
        if (wcm_fault) {
            latch |= OAMR_SC_LATCH_CHARGER;
        }
        if (now_us - sc->charge_on_us >= c->charge_interlock_us && safety_ok) {
            latch |= OAMR_SC_LATCH_INTERLOCK;
        }
        if (latch != 0u) {
            enter(sc, OAMR_SC_CHARGER_FAULT,
                  (latch & OAMR_SC_LATCH_CHARGER) ? OAMR_SC_R_CHARGER_FAULT : OAMR_SC_R_CHARGE_INTERLOCK);
            o->latches = latch;
            transitioned = true;
        }
    }

    /* 3. Other exits and entries. */
    if (!transitioned) {
        switch (o->state) {
        case OAMR_SC_RUN:
            if (exit_reasons != 0u) {
                enter(sc, OAMR_SC_LOCKED, top_reason(exit_reasons));
            }
            break;
        case OAMR_SC_CHARGING: {
            const uint32_t fault = exit_reasons & (OAMR_SC_R_ESTOP_DIAG | OAMR_SC_R_BMS_FAULT |
                                                   OAMR_SC_R_COMMS_LOST | OAMR_SC_R_INPUT_INVALID);
            if (fault != 0u) {
                enter(sc, OAMR_SC_LOCKED, top_reason(fault));
            } else if (!in->charge_cmd || !stopped) {
                enter(sc, OAMR_SC_LOCKED, 0u);
            }
            break;
        }
        case OAMR_SC_CHARGER_FAULT:
            if (short_press) {
                const bool wcm_ok = !(o->latches & OAMR_SC_LATCH_CHARGER) || !wcm_fault;
                if (!in->charge_cmd && wcm_ok) {
                    enter(sc, OAMR_SC_LOCKED, 0u); /* acknowledged; never arms */
                }
                short_press = false;
            }
            break;
        case OAMR_SC_LOCKED:
        default: {
            const bool arm_ok = !estop_diag_bad && !input_invalid && comms_ok && bms_ok && !in->charge_cmd;
            if (short_press && arm_ok) {
                sc->out.reset_window_open = true;
                sc->out.reset_seen = false;
                sc->window_start_us = now_us;
                sc->pnoz_did_not_enable = false;
            }
            short_press = false;
            if (o->reset_window_open) {
                if (safety_ok) {
                    o->reset_window_open = false;
                    o->reset_seen = true;
                } else if (now_us - sc->window_start_us >= c->reset_window_us) {
                    o->reset_window_open = false;
                    o->reset_seen = false;
                    if (!estop_base && !o->charge_request && !safety_ok) {
                        sc->pnoz_did_not_enable = true;
                    }
                }
            }
            if (charge_edge) {
                const bool charge_ok = stopped && !estop_diag_bad && !wcm_fault && bms_ok &&
                                       !input_invalid && comms_ok;
                if (charge_ok) {
                    enter(sc, OAMR_SC_CHARGING, 0u);
                    clear_reset_seen(sc);
                    sc->charge_on_us = now_us;
                    break;
                }
            }
            if (o->reset_seen && safety_ok && !estop_diag_bad && !input_invalid && comms_ok &&
                bms_ok && !in->charge_cmd && !in->sw_stop) {
                enter(sc, OAMR_SC_RUN, 0u);
            }
            break;
        }
        }
    }

    /* ---- outputs ---- */
    o->motion_allowed = (o->state == OAMR_SC_RUN);
    o->reset_lamp = (o->state != OAMR_SC_RUN);
    o->charge_request = (o->state == OAMR_SC_CHARGING);
    o->controller_reset = o->reboot_request && now_us >= sc->controller_reset_at_us;

    uint32_t r = exit_reasons;
    if (o->state == OAMR_SC_CHARGING) {
        r &= ~(uint32_t)OAMR_SC_R_HW_SAFETY_NOT_ENABLED; /* expected while KCHG opens the chain */
    }
    if (wcm_fault || (o->latches & OAMR_SC_LATCH_CHARGER)) r |= OAMR_SC_R_CHARGER_FAULT;
    if (o->latches & OAMR_SC_LATCH_INTERLOCK) r |= OAMR_SC_R_CHARGE_INTERLOCK;
    if (sc->press_active || o->reset_window_open) r |= OAMR_SC_R_RESET_IN_PROGRESS;
    if (sc->pnoz_did_not_enable) r |= OAMR_SC_R_PNOZ_DID_NOT_ENABLE;
    if (o->reboot_request) r |= OAMR_SC_R_REBOOT_REQUESTED;
    o->reasons = r;
    if (!sc->first_tick_done) {
        o->first_reason = top_reason(r);
        sc->first_tick_done = true;
    } else if (o->state != OAMR_SC_RUN && o->first_reason == 0u) {
        o->first_reason = top_reason(r);
    }
    return o;
}
