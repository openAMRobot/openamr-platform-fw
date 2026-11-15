/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
/*
 * Where the specification abstracts (events, condition inputs), this file chooses how raw
 * simulated pins, host messages and CAN frames become events. Each such choice is marked
 * "interpretation, not an owner ruling" and listed in tests/sim_reference/README.md.
 */
#include "simref/simref_ctrl.h"

#include <math.h>
#include <string.h>

#include "simref/simref_options.h"
#include "simref/simref_timing.h"

#define MS(x) ((int64_t)(x) * 1000)
#define RPM_TO_RAD_S (2.0 * 3.14159265358979323846 / 60.0)
#define MAX_RPM 1000
#define SLOT_CMD SIMREF_SUP_COUNT

bool simref_iface_allowed(const char *name)
{
    return name != NULL && strncmp(name, "vcan", 4) == 0;
}

void simref_ctrl_default_cfg(simref_ctrl_cfg_t *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->drive_node = 1;
    cfg->lift_node = 0x10;
    cfg->core_node = 0x01;
    cfg->wheel_radius_m = 0.100;
    cfg->wheel_separation_m = 0.400;
    cfg->left_sign = 1;
    cfg->right_sign = 1;
    cfg->boot_cause = SIMREF_CAUSE_POWER_ON;
    cfg->bms_ok = true;
    simref_permit_cfg_t *p = &cfg->permit;
    p->set = SIMREF_PP_ALL;
    p->stop_v_max = 0.01;  /* PROPOSED platform-fw #20 (C2), test value here */
    p->stop_w_max = 0.02;  /* PROPOSED platform-fw #20 (C2), test value here */
    p->stop_hold_us = MS(SIMREF_STOP_HOLD_MS);
    p->wheel_stale_us = MS(SIMREF_ENCODER_STALE_MS);
    p->arm_stale_us = MS(SIMREF_ARM_STALE_MS);
    p->track_m = cfg->wheel_separation_m;
#if SIMREF_OPT_C8_STOWED_ONLY
    p->approved_poses = 1u << SIMREF_POSE_STOWED;
#else
    p->approved_poses = (1u << SIMREF_POSE_STOWED) | (1u << SIMREF_POSE_CARRY);
#endif
}

static void log_reasons(simref_ctrl_t *c, int64_t now)
{
    for (unsigned i = 0; i < c->sm.n_reasons; i++) {
        simref_reason_log_t *e = &c->log[c->log_n % SIMREF_CTRL_LOG];
        e->t_us = now;
        e->reason = c->sm.reasons[i];
        c->log_n++;
    }
}

static void queue(simref_ctrl_t *c, simref_event_t ev)
{
    if (ev.id == SIMREF_EV_MOTION_CMD) {
        for (int i = 0; i < c->nq; i++)
            if (c->q[i].id == SIMREF_EV_MOTION_CMD) {
                c->q[i] = ev; /* only the latest motion command of a tick is kept */
                return;
            }
    }
    if (c->nq < SIMREF_CTRL_QUEUE)
        c->q[c->nq++] = ev;
    else
        c->q_overflow = true;
}

static void reset_runtime(simref_ctrl_t *c, int64_t now)
{
    simref_send_fn send = c->send;
    void *ctx = c->send_ctx;
    simref_ctrl_cfg_t cfg = c->cfg;
    uint32_t reboots = c->reboots;
    bool raw[SIMREF_PIN_COUNT];
    bool pins_known = c->pins_known;
    memcpy(raw, c->raw, sizeof raw);
    memset(c, 0, sizeof *c);
    c->cfg = cfg;
    c->send = send;
    c->send_ctx = ctx;
    c->reboots = reboots;
    c->boot_us = now;
    /* the simulated pins keep their levels across a controller reboot */
    c->pins_known = pins_known;
    memcpy(c->raw, raw, sizeof raw);
    memcpy(c->deb, raw, sizeof raw);
    for (int i = 0; i < SIMREF_PIN_COUNT; i++)
        c->raw_since_us[i] = now;
    c->reset_low_seen = pins_known && !raw[SIMREF_PIN_RESET];
    simref_sm_init(&c->sm);
    simref_drive_cfg_t dc = {
        .node_id = cfg.drive_node,
        .hb_consumer_us = MS(SIMREF_DRIVE_HB_CONSUMER_MS),
        .sdo_timeout_us = MS(SIMREF_SDO_TIMEOUT_MS),
        .transition_limit_us = MS(SIMREF_CIA402_TRANSITION_LIMIT_MS),
        .period_us = MS(SIMREF_DRIVE_PERIOD_MS),
        .status_every = SIMREF_DRIVE_STATUS_EVERY_N_CYCLES,
#if SIMREF_OPT_C1_DRIVE_TIMING_SIDE_B
        .expect_1017 = 100, .expect_2000 = 250,
#else
        .expect_1017 = 200, .expect_2000 = 500, /* C1 side a. Interpretation, not an owner ruling. */
#endif
    };
    simref_drive_init(&c->drive, &dc, send, ctx);
    simref_permit_init(&c->permit, &cfg.permit);
    simref_permit_source_session(&c->permit, SIMREF_SRC_ARMS, 1u);
    c->permit_tx.boot_id = (uint16_t)(((uint64_t)now >> 4) ^ (reboots * 0x9E37u));
    if (c->permit_tx.boot_id == 0)
        c->permit_tx.boot_id = 1;
    c->core_hb_us = c->lift_ka_us = c->permit_tx_us = now;
    /* boot event (T1) */
    simref_event_t boot = {.id = SIMREF_EV_BOOT, .cause = (uint8_t)cfg.boot_cause};
    simref_sm_inputs_t in = simref_ctrl_inputs(c, now);
    c->last_err = simref_sm_tick(&c->sm, &in, &boot, 1);
    log_reasons(c, now);
}

void simref_ctrl_init(simref_ctrl_t *c, const simref_ctrl_cfg_t *cfg, int64_t now, simref_send_fn send, void *ctx)
{
    memset(c, 0, sizeof *c);
    c->cfg = *cfg;
    c->send = send;
    c->send_ctx = ctx;
    reset_runtime(c, now);
}

static void tx(simref_ctrl_t *c, simref_bus_t bus, uint32_t id, const uint8_t *d, uint8_t len)
{
    simref_frame_t f = {.id = id, .len = len};
    memcpy(f.data, d, len);
    if (c->send)
        c->send(c->send_ctx, bus, &f);
}

/* ---- condition inputs ---------------------------------------------------------------- */
static bool session_fresh(const simref_ctrl_t *c, int64_t now)
{
    return c->session_up && now - c->session_hb_us < MS(SIMREF_SESSION_TIMEOUT_MS);
}

static bool encoders_fresh(const simref_ctrl_t *c, int64_t now)
{
    for (int i = 0; i < 2; i++)
        if (!c->drive.act_valid[i] || now - c->drive.act_rx_us[i] >= MS(SIMREF_ENCODER_STALE_MS))
            return false;
    return true;
}

static bool base_stopped(const simref_ctrl_t *c, int64_t now)
{
    return encoders_fresh(c, now) && c->stopped_raw && now - c->stopped_since_us >= MS(SIMREF_STOP_HOLD_MS);
}

simref_sm_inputs_t simref_ctrl_inputs(const simref_ctrl_t *c, int64_t now)
{
    simref_sm_inputs_t in = {0};
    bool y32 = c->pins_known && c->deb[SIMREF_PIN_Y32];
    bool estop_released = c->pins_known && !c->deb[SIMREF_PIN_ESTOP_CONTACT];
    bool inputs_valid = c->pins_known && !c->contactor_reported && encoders_fresh(c, now);
    in.run_ok = y32 && estop_released && session_fresh(c, now) && c->cfg.bms_ok &&
                simref_drive_enabled(&c->drive) && inputs_valid;
    in.base_stopped = base_stopped(c, now);
    /* arming at the release edge: RESET seen low since boot, E-stop released, no charge
     * command. Interpretation, not an owner ruling. */
    in.arming_ok = c->reset_low_seen && estop_released && !c->sm.charge_cmd_present;
    /* 5.2 qualification folded: SAFETY_OK debounced high at the release edge.
     * Interpretation, not an owner ruling. */
    in.qualified = y32;
    in.charge_pre_ok = in.base_stopped && session_fresh(c, now) && c->cfg.bms_ok && c->pins_known &&
                       !c->raw[SIMREF_PIN_CHARGER_FAULT] && !c->sm.latches.wcm_latched &&
                       !c->sm.latches.interlock_latched && estop_released;
    return in;
}

/* ---- pins ---------------------------------------------------------------------------- */
void simref_ctrl_set_pin(simref_ctrl_t *c, simref_pin_t pin, bool level, int64_t now)
{
    if ((unsigned)pin >= SIMREF_PIN_COUNT)
        return;
    if (!c->pins_known) {
        /* the first complete pin set initialises the debounced levels without edges */
        c->raw[pin] = level;
        c->deb[pin] = level;
        c->raw_since_us[pin] = now;
        c->pins_seen |= 1u << pin;
        if (c->pins_seen == (1u << SIMREF_PIN_COUNT) - 1u) {
            c->pins_known = true;
            c->reset_low_seen = !c->raw[SIMREF_PIN_RESET];
        }
        return;
    }
    if (c->raw[pin] != level) {
        c->raw[pin] = level;
        c->raw_since_us[pin] = now;
    }
}

static void reset_released(simref_ctrl_t *c, int64_t now)
{
    int64_t held = now - c->reset_press_us;
    if (held < MS(SIMREF_RESET_INVALID_MS))
        queue(c, (simref_event_t){.id = SIMREF_EV_RESET_SHORT});
}

static void on_deb_edge(simref_ctrl_t *c, simref_pin_t pin, bool level, int64_t now)
{
    switch (pin) {
    case SIMREF_PIN_Y32:
        if (!level)
            queue(c, (simref_event_t){.id = SIMREF_EV_SAFETY_OK_LOW});
        break;
    case SIMREF_PIN_ESTOP_CONTACT:
        if (level)
            queue(c, (simref_event_t){.id = SIMREF_EV_ESTOP_DIAG});
        break;
    case SIMREF_PIN_CHARGER_FAULT:
        queue(c, (simref_event_t){.id = level ? SIMREF_EV_WCM_FAULT : SIMREF_EV_WCM_FAULT_CLEAR});
        break;
    case SIMREF_PIN_RESET:
        if (level) {
            c->reset_pressed = true;
            c->reset_press_us = now;
            c->reset_invalid_sent = c->reset_reboot_sent = false;
            /* Press edge in LOCKED with SAFETY_OK high and the E-stop released starts a new
             * CiA 402 enable sequence, so that the release edge can enter RUN. Nothing enables
             * the drive automatically. Interpretation, not an owner ruling. */
            if (c->sm.state == SIMREF_ST_LOCKED && c->deb[SIMREF_PIN_Y32] && !c->deb[SIMREF_PIN_ESTOP_CONTACT] &&
                c->reset_low_seen)
                simref_drive_request_enable(&c->drive, now);
        } else {
            if (c->reset_pressed)
                reset_released(c, now);
            c->reset_pressed = false;
            c->reset_low_seen = true;
            c->release_pending = true;
        }
        break;
    default:
        break;
    }
}

static void process_pins(simref_ctrl_t *c, int64_t now)
{
    if (!c->pins_known)
        return;
    for (int p = 0; p < SIMREF_PIN_COUNT; p++) {
        if (c->raw[p] == c->deb[p])
            continue;
        /* Y32 high to low acts at once (rev 5 5.2); every other edge after the debounce time. */
        bool at_once = (p == SIMREF_PIN_Y32 && !c->raw[p]) || p == SIMREF_PIN_CONTACTOR_FB;
        if (at_once || now - c->raw_since_us[p] >= MS(SIMREF_INPUT_DEBOUNCE_MS)) {
            c->deb[p] = c->raw[p];
            on_deb_edge(c, (simref_pin_t)p, c->deb[p], now);
        }
    }
    if (c->reset_pressed) {
        int64_t held = now - c->reset_press_us;
        if (held >= MS(SIMREF_RESET_INVALID_MS) && !c->reset_invalid_sent) {
            c->reset_invalid_sent = true;
            queue(c, (simref_event_t){.id = SIMREF_EV_INPUT_INVALID, .subcause = SIMREF_SUB_RESET_HIGH});
        }
        if (held >= MS(SIMREF_RESET_REBOOT_MS) && !c->reset_reboot_sent) {
            c->reset_reboot_sent = true;
            queue(c, (simref_event_t){.id = SIMREF_EV_RESET_HOLD_5S});
        }
    }
    /* Contactor feedback against the commanded state. The commanded state is taken as the
     * SAFETY_OK level (the hardwired chain closes the contactor). The specification has no
     * contactor event (gap, see simref_options.h). Interpretation, not an owner ruling. */
    bool expected = c->deb[SIMREF_PIN_Y32];
    bool mismatch = c->raw[SIMREF_PIN_CONTACTOR_FB] != expected;
    if (mismatch && !c->contactor_mismatch) {
        c->contactor_mismatch = true;
        c->contactor_mismatch_since_us = now;
    } else if (!mismatch) {
        c->contactor_mismatch = false;
        c->contactor_reported = false;
    }
    if (c->contactor_mismatch && !c->contactor_reported &&
        now - c->contactor_mismatch_since_us >= MS(SIMREF_CONTACTOR_SETTLE_MS)) {
        c->contactor_reported = true;
        c->input_detail = SIMREF_INPUT_DETAIL_CONTACTOR_FEEDBACK;
#if SIMREF_OPT_CONTACTOR_MISMATCH_EVENT == 2
        queue(c, (simref_event_t){.id = SIMREF_EV_ESTOP_DIAG});
#else
        queue(c, (simref_event_t){.id = SIMREF_EV_INPUT_INVALID, .subcause = SIMREF_SUB_IMPLAUSIBLE});
#endif
    }
}

/* ---- charge request logic ------------------------------------------------------------ */
static void process_charge(simref_ctrl_t *c, int64_t now)
{
    bool charging = c->sm.state == SIMREF_ST_CHARGING;
    if (charging && !c->charging_prev) {
        c->charge_request_on_us = now;
        c->interlock_window_checked = c->interlock_reported = false;
        c->base_not_stopped_reported = false;
        c->y32_high_since_us = -1;
    }
    c->charging_prev = charging;
    if (!charging || c->interlock_reported)
        return;
    bool y32 = c->raw[SIMREF_PIN_Y32];
    if (!c->interlock_window_checked) {
        if (now - c->charge_request_on_us >= MS(SIMREF_CHARGE_INTERLOCK_WINDOW_MS)) {
            c->interlock_window_checked = true;
            if (c->deb[SIMREF_PIN_Y32]) {
                c->interlock_reported = true;
                queue(c, (simref_event_t){.id = SIMREF_EV_CHARGE_INTERLOCK});
                return;
            }
        }
    } else {
        if (y32 && c->y32_high_since_us < 0)
            c->y32_high_since_us = now;
        if (!y32)
            c->y32_high_since_us = -1;
        if (c->y32_high_since_us >= 0 && now - c->y32_high_since_us >= MS(SIMREF_CHARGE_INTERLOCK_HIGH_MS)) {
            c->interlock_reported = true;
            queue(c, (simref_event_t){.id = SIMREF_EV_CHARGE_INTERLOCK});
        }
    }
    if (!c->base_not_stopped_reported && encoders_fresh(c, now) && !c->stopped_raw) {
        c->base_not_stopped_reported = true;
        queue(c, (simref_event_t){.id = SIMREF_EV_BASE_NOT_STOPPED});
    }
}

/* ---- host messages ------------------------------------------------------------------- */
void simref_ctrl_host_session(simref_ctrl_t *c, uint32_t epoch, int64_t now)
{
    if (c->session_up && epoch == c->session_epoch) {
        c->session_hb_us = now;
        return;
    }
    c->session_up = true;
    c->session_epoch = epoch;
    c->session_hb_us = now;
    memset(c->seq_has, 0, sizeof c->seq_has);
}

void simref_ctrl_host_heartbeat(simref_ctrl_t *c, uint32_t epoch, int64_t now)
{
    if (c->session_up && epoch == c->session_epoch)
        c->session_hb_us = now;
}

static void filter(simref_ctrl_t *c, int slot, uint32_t epoch, uint32_t seq, simref_event_t *ev)
{
    ev->session_old = !c->session_up || epoch != c->session_epoch;
    ev->seq_replay = !ev->session_old && c->seq_has[slot] && seq <= c->seq_last[slot];
    if (!ev->session_old && !ev->seq_replay) {
        c->seq_has[slot] = true;
        c->seq_last[slot] = seq;
    }
}

void simref_ctrl_host_cmd(simref_ctrl_t *c, uint32_t epoch, uint32_t seq, double v, double w, int64_t now)
{
    simref_event_t ev = {.id = SIMREF_EV_MOTION_CMD};
    filter(c, SLOT_CMD, epoch, seq, &ev);
    if (!ev.session_old && !ev.seq_replay) {
        c->cmd_v = v;
        c->cmd_w = w;
        c->cmd_us = now;
        c->cmd_valid_seen = true;
        c->stale_reported = false;
        c->cmd_accepted++;
    } else {
        c->cmd_rejected++;
    }
    queue(c, ev);
}

void simref_ctrl_host_supervisory(simref_ctrl_t *c, simref_sup_t kind, uint32_t epoch, uint32_t seq, int64_t now)
{
    static const simref_event_id_t MAP[SIMREF_SUP_COUNT] = {
        SIMREF_EV_UI_STOP, SIMREF_EV_UI_RESUME, SIMREF_EV_JETSON_STOP_RELEASE, SIMREF_EV_CHARGE_CMD,
        SIMREF_EV_CHARGE_CMD_REMOVED, SIMREF_EV_CHARGER_ACK};
    (void)now;
    if ((unsigned)kind >= SIMREF_SUP_COUNT)
        return;
    simref_event_t ev = {.id = MAP[kind]};
    filter(c, (int)kind, epoch, seq, &ev);
    queue(c, ev);
}

void simref_ctrl_host_lift(simref_ctrl_t *c, uint8_t op, uint8_t stop, int64_t now)
{
    c->lift_seq = (uint8_t)(c->lift_seq + 1u);
    c->lift_cmd[0] = c->lift_seq;
    c->lift_cmd[1] = op;
    c->lift_cmd[2] = stop;
    c->lift_cmd_valid = true;
    tx(c, SIMREF_BUS_AUX, 0x200u + c->cfg.lift_node, c->lift_cmd, 3);
    c->lift_ka_us = now;
}

void simref_ctrl_host_arms(simref_ctrl_t *c, const simref_arm_t *l, const simref_arm_t *r, simref_pose_t bimanual,
                           int64_t now)
{
    c->arms_seq++;
    simref_permit_arms(&c->permit, now, 1u, c->arms_seq, true, l, r, bimanual);
}

/* ---- CAN frames ---------------------------------------------------------------------- */
void simref_ctrl_on_frame(simref_ctrl_t *c, simref_bus_t bus, const simref_frame_t *f, int64_t now)
{
    if (bus == SIMREF_BUS_DRIVE) {
        simref_drive_on_frame(&c->drive, f, now);
        return;
    }
    if (f->id == 0x700u + c->cfg.lift_node && f->len >= 1) {
        c->lift_hb_seen = true;
        c->lift_hb_us = now;
        if (f->data[0] != 0x05 && c->lift_nmt == 0x05)
            c->lift_cmd_valid = false; /* never replay a command into a restarted node */
        c->lift_nmt = f->data[0];
    } else if (f->id == 0x180u + c->cfg.lift_node && f->len == 8) {
        memcpy(c->lift_status, f->data, 8);
        c->lift_status_valid = true;
    }
}

/* ---- wheel evidence, odometry, stopped ----------------------------------------------- */
static void process_wheels(simref_ctrl_t *c, int64_t now)
{
    simref_drive_t *d = &c->drive;
    if (d->epoch != c->wheel_epoch) {
        c->wheel_epoch = d->epoch;
        simref_permit_source_session(&c->permit, SIMREF_SRC_LEFT, d->epoch);
        simref_permit_source_session(&c->permit, SIMREF_SRC_RIGHT, d->epoch);
        c->odom_seq_seen[0] = d->act_seq[0];
        c->odom_seq_seen[1] = d->act_seq[1];
    }
    bool fresh_pair = d->act_seq[0] != c->odom_seq_seen[0] && d->act_seq[1] != c->odom_seq_seen[1];
    if (!fresh_pair)
        return;
    c->odom_seq_seen[0] = d->act_seq[0];
    c->odom_seq_seen[1] = d->act_seq[1];
    int signs[2] = {c->cfg.left_sign, c->cfg.right_sign};
    for (int i = 0; i < 2; i++) {
        c->wheel_rad_s[i] = signs[i] * (d->act_rpm10[i] / 10.0) * RPM_TO_RAD_S;
        simref_permit_wheel(&c->permit, now, (simref_src_t)i, d->epoch, d->act_seq[i], d->act_valid[i],
                            c->wheel_rad_s[i] * c->cfg.wheel_radius_m);
    }
    double r = c->cfg.wheel_radius_m;
    c->v = r * (c->wheel_rad_s[0] + c->wheel_rad_s[1]) / 2.0;
    c->w = r * (c->wheel_rad_s[1] - c->wheel_rad_s[0]) / c->cfg.wheel_separation_m;
    if (c->odom_valid) {
        double dt = (double)(now - c->odom_us) / 1e6;
        c->yaw += c->w * dt;
        c->x += c->v * cos(c->yaw) * dt;
        c->y += c->v * sin(c->yaw) * dt;
        c->wheel_pos[0] += c->wheel_rad_s[0] * dt;
        c->wheel_pos[1] += c->wheel_rad_s[1] * dt;
    }
    c->odom_valid = true;
    c->odom_us = now;
#if SIMREF_OPT_C2_STOPPED_BY_TWIST
    bool st = fabs(c->v) < c->cfg.permit.stop_v_max && fabs(c->w) < c->cfg.permit.stop_w_max;
#else
    bool st = fabs(d->act_rpm10[0]) < 10 && fabs(d->act_rpm10[1]) < 10; /* C2 side a: below 1 rpm */
#endif
    if (st && !c->stopped_raw)
        c->stopped_since_us = now;
    c->stopped_raw = st;
}

/* ---- lift bus ------------------------------------------------------------------------ */
static void process_lift(simref_ctrl_t *c, int64_t now)
{
    if (now - c->core_hb_us >= MS(SIMREF_CORE_HB_PERIOD_MS)) {
        c->core_hb_us = now;
        uint8_t b = 0x05;
        tx(c, SIMREF_BUS_AUX, 0x700u + c->cfg.core_node, &b, 1);
    }
    if (c->lift_hb_seen && c->lift_nmt != 0x05 && now - c->lift_nmt_us >= MS(500)) {
        c->lift_nmt_us = now;
        c->lift_cmd_valid = false;
        uint8_t b[2] = {0x01, c->cfg.lift_node};
        tx(c, SIMREF_BUS_AUX, 0x000, b, 2);
    }
    if (c->lift_cmd_valid && now - c->lift_ka_us >= MS(SIMREF_LIFT_KEEPALIVE_MS)) {
        c->lift_ka_us = now;
        tx(c, SIMREF_BUS_AUX, 0x200u + c->cfg.lift_node, c->lift_cmd, 3);
    }
    bool link = c->lift_hb_seen && c->lift_nmt == 0x05 && now - c->lift_hb_us < MS(SIMREF_CTL017_HB_CONSUMER_MS);
    simref_permit_set_ctl017_link(&c->permit, link);
    /* The base reports a fault to the permit producer in every lock state.
     * Interpretation, not an owner ruling. */
    simref_permit_set_base_fault(&c->permit, c->sm.state == SIMREF_ST_LOCKED ||
                                                 c->sm.state == SIMREF_ST_CHARGER_FAULT);
    if (now - c->permit_tx_us >= MS(SIMREF_PERMIT_PERIOD_MS)) {
        c->permit_tx_us = now;
        simref_permit_evaluate(&c->permit, now);
        simref_permit_begin_step(&c->permit);
        uint8_t b[8];
        size_t n = simref_permit_encode(&c->permit_tx, c->permit.permit, (uint8_t)c->permit.reason, b);
        tx(c, SIMREF_BUS_AUX, SIMREF_PERMIT_COB_ID, b, (uint8_t)n);
    }
}

/* ---- the tick ------------------------------------------------------------------------ */
static int32_t rpm_of(double rad_s, int sign)
{
    double rpm = sign * rad_s / RPM_TO_RAD_S;
    if (rpm > MAX_RPM)
        rpm = MAX_RPM;
    if (rpm < -MAX_RPM)
        rpm = -MAX_RPM;
    return (int32_t)lround(rpm);
}

static void update_targets(simref_ctrl_t *c)
{
    int32_t l = 0, r = 0;
    if (c->sm.state == SIMREF_ST_RUN && c->sm.motor_follow && simref_drive_enabled(&c->drive)) {
        double half = c->cmd_w * c->cfg.wheel_separation_m / 2.0;
        l = rpm_of((c->cmd_v - half) / c->cfg.wheel_radius_m, c->cfg.left_sign);
        r = rpm_of((c->cmd_v + half) / c->cfg.wheel_radius_m, c->cfg.right_sign);
    }
    c->target_rpm[0] = l;
    c->target_rpm[1] = r;
    simref_drive_set_targets(&c->drive, l, r);
}

static void run_tick(simref_ctrl_t *c, int64_t now)
{
    simref_state_t before = c->sm.state;
    simref_sm_inputs_t in = simref_ctrl_inputs(c, now);
    simref_sm_err_t e = simref_sm_tick(&c->sm, &in, c->q, (size_t)c->nq);
    c->nq = 0;
    if (e != SIMREF_SM_OK) {
        c->last_err = e;
        c->spec_errors++;
    }
    log_reasons(c, now);
    if (c->sm.state == SIMREF_ST_RUN && before != SIMREF_ST_RUN)
        c->run_entry_us = now;
    bool active = c->sm.state == SIMREF_ST_RUN || c->sm.state == SIMREF_ST_INHIBITED;
    bool was_active = before == SIMREF_ST_RUN || before == SIMREF_ST_INHIBITED;
    if (was_active && !active)
        simref_drive_disable(&c->drive, now);
}

void simref_ctrl_step(simref_ctrl_t *c, int64_t now)
{
    process_pins(c, now);
    simref_drive_step(&c->drive, now);
    simref_dl_detail_t det;
    for (simref_dl_event_t de; (de = simref_drive_pop_event(&c->drive, &det)) != SIMREF_DLE_NONE;) {
        if (de == SIMREF_DLE_FAULT)
            queue(c, (simref_event_t){.id = SIMREF_EV_DRIVE_FAULT});
        else
            queue(c, (simref_event_t){.id = SIMREF_EV_DRIVE_COMMS,
                                      .kind = de == SIMREF_DLE_CONFIG ? SIMREF_KIND_CONFIG : SIMREF_KIND_COMMS});
    }
    process_wheels(c, now);
    if (c->session_up && now - c->session_hb_us >= MS(SIMREF_SESSION_TIMEOUT_MS)) {
        c->session_up = false;
        queue(c, (simref_event_t){.id = SIMREF_EV_SESSION_LOST});
    }
    /* command watchdog: only while RUN follows a command */
    if (c->sm.state == SIMREF_ST_RUN && c->sm.motor_follow && !c->stale_reported &&
        now - c->cmd_us >= MS(SIMREF_CMD_WATCHDOG_MS)) {
        c->stale_reported = true;
        queue(c, (simref_event_t){.id = SIMREF_EV_CMD_STALE});
    }
    process_charge(c, now);
    if (c->nq > 0 || now >= c->next_tick_us) {
        run_tick(c, now);
        c->next_tick_us = now + MS(SIMREF_TICK_MS);
    }
    if (c->release_pending) {
        c->release_pending = false;
        /* after a RESET release that did not lead to RUN, the drive is disabled again */
        if (c->sm.state != SIMREF_ST_RUN && c->sm.state != SIMREF_ST_INHIBITED)
            simref_drive_disable(&c->drive, now);
        if (c->sm.reboot_pending) {
            c->reboots++;
            c->cfg.boot_cause = SIMREF_CAUSE_SOFTWARE;
            reset_runtime(c, now);
            return;
        }
    }
    update_targets(c);
    simref_drive_step(&c->drive, now); /* a zero target is written at once */
    process_lift(c, now);
}

int64_t simref_ctrl_next_deadline(const simref_ctrl_t *c)
{
    int64_t t = c->next_tick_us;
    int64_t d = simref_drive_next_deadline(&c->drive);
    if (d < t)
        t = d;
    if (c->sm.state == SIMREF_ST_RUN && c->sm.motor_follow && !c->stale_reported) {
        int64_t w = c->cmd_us + MS(SIMREF_CMD_WATCHDOG_MS);
        if (w < t)
            t = w;
    }
    int64_t p = c->permit_tx_us + MS(SIMREF_PERMIT_PERIOD_MS);
    if (p < t)
        t = p;
    return t;
}
