/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
/*
 * Table of docs/safety/spec/state_machine.yaml version 2, rows in file order. Row ids are
 * the YAML ids; tests/sim_reference/adapter/test_table_structure.py compares id, from, event
 * and to of every row with the YAML file. Rows whose behaviour depends on an open conflict
 * or a tick-semantics reading are selected by simref_options.h.
 */
#include "simref/simref_sm.h"

#include <string.h>

#include "simref/simref_options.h"

#define ST(s) (1u << (SIMREF_ST_##s))
#define ALL_ST (ST(LOCKED) | ST(RUN) | ST(INHIBITED) | ST(CHARGING) | ST(CHARGER_FAULT))
#define EV(e) (1u << (SIMREF_EV_##e))
#define SUPERVISORY (EV(UI_RESUME) | EV(JETSON_STOP_RELEASE) | EV(CHARGE_CMD) | EV(CHARGER_ACK) | EV(MOTION_CMD))

/* tri-state: 0 = not tested or unchanged, 1 = true or set, 2 = false or clear */
enum { T_ANY = 0, T_YES = 1, T_NO = 2 };

/* guard keys */
enum {
    G_CHARGE_CMD_PRESENT = 0, G_ARMING_OK, G_QUALIFIED, G_UI_STOP, G_RESET_SEEN, G_JETSON_STOP,
    G_RUN_OK, G_BASE_STOPPED, G_CHARGE_PRE_OK, G_WCM_LATCHED, G_INTERLOCK_LATCHED,
    G_REBOOT_PENDING, G_COUNT
};
#define GD(k, v) [G_##k] = (v)

/* reason templates */
#define RT_BOOT (-1)
#define RT_INPUT (-2)
#define RT_DRIVE (-3)

enum { MOTOR_KEEP = 0, MOTOR_ZERO, MOTOR_FOLLOW };

typedef struct {
    const char *id;
    uint32_t from;
    uint32_t events;
    uint8_t when_session_old, when_seq_replay, when_queued;  /* tri-state */
    uint8_t guard[G_COUNT];                                  /* tri-state */
    int8_t to;                                               /* -1 stay */
    uint8_t set_reset_seen, set_ui_stop, set_jetson_stop, set_wcm_latched, set_interlock_latched;
    uint8_t var_charge_cmd, var_wcm_active, var_reboot_pending;
    uint8_t motor;
    int8_t reason;    /* simref_reason_t, RT_* template, or 0 */
    bool record;
    uint8_t result;   /* simref_result_t or 0 */
    bool ack;
    int8_t then;      /* simref_event_id_t or -1 */
    bool run_entry;
    bool q1_extra;    /* C3: wcm_active blocks this row when wcm_blocks_run */
} row_t;

#define NOTHEN .then = -1
#define STAY .to = -1

#if SIMREF_OPT_C6_DRIVE_FAULT_IN_CHARGING_STAYS
#define DRIVE_TO_LOCKED (ST(RUN) | ST(INHIBITED))
#define DRIVE_STAY (ST(LOCKED) | ST(CHARGING) | ST(CHARGER_FAULT))
#else
#define DRIVE_TO_LOCKED (ST(RUN) | ST(INHIBITED) | ST(CHARGING)) /* C6 side b: interpretation, not an owner ruling */
#define DRIVE_STAY (ST(LOCKED) | ST(CHARGER_FAULT))
#endif

static const row_t ROWS[] = {
    {.id = "B1", .from = ALL_ST, .events = EV(BOOT), .to = SIMREF_ST_LOCKED, .set_reset_seen = T_NO,
     .set_ui_stop = T_NO, .set_jetson_stop = T_YES, .set_wcm_latched = T_NO, .set_interlock_latched = T_NO,
     .var_charge_cmd = T_NO, .var_reboot_pending = T_NO, .motor = MOTOR_ZERO, .reason = RT_BOOT, .record = true, NOTHEN},
    {.id = "S1", .from = ALL_ST, .events = SUPERVISORY, .when_session_old = T_YES, STAY,
     .result = SIMREF_RES_REJECTED, .reason = SIMREF_R_SESSION_OLD, NOTHEN},
    {.id = "S2", .from = ALL_ST, .events = SUPERVISORY, .when_seq_replay = T_YES, STAY,
     .result = SIMREF_RES_REJECTED, .reason = SIMREF_R_SEQ_REJECTED, NOTHEN},
    {.id = "U1", .from = ST(RUN), .events = EV(UI_STOP), .to = SIMREF_ST_INHIBITED, .set_ui_stop = T_YES,
     .motor = MOTOR_ZERO, .reason = SIMREF_R_UI_STOP, NOTHEN},
    {.id = "U2", .from = ST(LOCKED) | ST(INHIBITED) | ST(CHARGING) | ST(CHARGER_FAULT), .events = EV(UI_STOP), STAY,
     .set_ui_stop = T_YES, .reason = SIMREF_R_UI_STOP, NOTHEN},
    {.id = "H1", .from = ST(RUN) | ST(INHIBITED), .events = EV(SAFETY_OK_LOW), .to = SIMREF_ST_LOCKED,
     .set_reset_seen = T_NO, .motor = MOTOR_ZERO, .reason = SIMREF_R_HW_SAFETY_NOT_ENABLED, .record = true, NOTHEN},
    {.id = "H2", .from = ST(LOCKED), .events = EV(SAFETY_OK_LOW), STAY, .set_reset_seen = T_NO,
     .reason = SIMREF_R_HW_SAFETY_NOT_ENABLED, .record = true, NOTHEN},
    {.id = "H3", .from = ST(CHARGING), .events = EV(SAFETY_OK_LOW), STAY, NOTHEN},
    {.id = "H4", .from = ST(CHARGER_FAULT), .events = EV(SAFETY_OK_LOW), STAY,
     .reason = SIMREF_R_HW_SAFETY_NOT_ENABLED, .record = true, NOTHEN},
    {.id = "E1", .from = ST(RUN) | ST(INHIBITED) | ST(CHARGING), .events = EV(ESTOP_DIAG), .to = SIMREF_ST_LOCKED,
     .set_reset_seen = T_NO, .motor = MOTOR_ZERO, .reason = SIMREF_R_ESTOP_DIAG, .record = true, NOTHEN},
    {.id = "E2", .from = ST(LOCKED) | ST(CHARGER_FAULT), .events = EV(ESTOP_DIAG), STAY, .set_reset_seen = T_NO,
     .reason = SIMREF_R_ESTOP_DIAG, .record = true, NOTHEN},
    {.id = "J1", .from = ST(RUN) | ST(INHIBITED) | ST(CHARGING), .events = EV(SESSION_LOST), .to = SIMREF_ST_LOCKED,
     .set_reset_seen = T_NO, .set_jetson_stop = T_YES, .var_charge_cmd = T_NO, .motor = MOTOR_ZERO,
     .reason = SIMREF_R_COMMS_LOST, .record = true, NOTHEN},
    {.id = "J2", .from = ST(LOCKED) | ST(CHARGER_FAULT), .events = EV(SESSION_LOST), STAY, .set_reset_seen = T_NO,
     .set_jetson_stop = T_YES, .var_charge_cmd = T_NO, .reason = SIMREF_R_COMMS_LOST, .record = true, NOTHEN},
    {.id = "M1", .from = ST(RUN) | ST(INHIBITED) | ST(CHARGING), .events = EV(BMS_FAULT), .to = SIMREF_ST_LOCKED,
     .set_reset_seen = T_NO, .motor = MOTOR_ZERO, .reason = SIMREF_R_BMS_FAULT, .record = true, NOTHEN},
    {.id = "M2", .from = ST(LOCKED) | ST(CHARGER_FAULT), .events = EV(BMS_FAULT), STAY, .set_reset_seen = T_NO,
     .reason = SIMREF_R_BMS_FAULT, .record = true, NOTHEN},
    {.id = "I1", .from = ST(RUN) | ST(INHIBITED) | ST(CHARGING), .events = EV(INPUT_INVALID), .to = SIMREF_ST_LOCKED,
     .set_reset_seen = T_NO, .motor = MOTOR_ZERO, .reason = RT_INPUT, .record = true, NOTHEN},
    {.id = "I2", .from = ST(LOCKED) | ST(CHARGER_FAULT), .events = EV(INPUT_INVALID), STAY, .set_reset_seen = T_NO,
     .reason = RT_INPUT, .record = true, NOTHEN},
    {.id = "R1", .from = ALL_ST, .events = EV(RESET_HOLD_5S), .to = SIMREF_ST_LOCKED, .set_reset_seen = T_NO,
     .var_reboot_pending = T_YES, .motor = MOTOR_ZERO, .reason = SIMREF_R_REBOOT_REQUESTED, .record = true, NOTHEN},
    {.id = "W1", .from = ST(CHARGING), .events = EV(WCM_FAULT), .to = SIMREF_ST_CHARGER_FAULT, .set_wcm_latched = T_YES,
     .var_wcm_active = T_YES, .reason = SIMREF_R_CHARGER_FAULT, .record = true, NOTHEN},
    {.id = "W2", .from = ST(LOCKED) | ST(RUN) | ST(INHIBITED) | ST(CHARGER_FAULT), .events = EV(WCM_FAULT), STAY,
     .var_wcm_active = T_YES, .reason = SIMREF_R_CHARGER_FAULT, .record = true, NOTHEN},
    {.id = "W3", .from = ALL_ST, .events = EV(WCM_FAULT_CLEAR), STAY, .var_wcm_active = T_NO, NOTHEN},
    {.id = "K1", .from = ST(CHARGING), .events = EV(CHARGE_INTERLOCK), .to = SIMREF_ST_CHARGER_FAULT,
     .set_interlock_latched = T_YES, .reason = SIMREF_R_CHARGE_INTERLOCK, .record = true, NOTHEN},
    {.id = "K2", .from = ST(CHARGER_FAULT), .events = EV(CHARGE_INTERLOCK), STAY, .set_interlock_latched = T_YES,
     .reason = SIMREF_R_CHARGE_INTERLOCK, .record = true, NOTHEN},
#if !SIMREF_OPT_C5_INTERLOCK_ONLY_IN_ATTEMPT
    /* C5 side a (H21): latches in any state. Interpretation, not an owner ruling. */
    {.id = "K3-C5a", .from = ST(LOCKED) | ST(RUN) | ST(INHIBITED), .events = EV(CHARGE_INTERLOCK), STAY,
     .set_interlock_latched = T_YES, .reason = SIMREF_R_CHARGE_INTERLOCK, .record = true, NOTHEN},
#endif
    {.id = "D1", .from = DRIVE_TO_LOCKED, .events = EV(DRIVE_FAULT), .to = SIMREF_ST_LOCKED, .set_reset_seen = T_NO,
     .motor = MOTOR_ZERO, .reason = SIMREF_R_DRIVE_FAULT, .record = true, NOTHEN},
    {.id = "D2", .from = DRIVE_STAY, .events = EV(DRIVE_FAULT), STAY, .set_reset_seen = T_NO,
     .reason = SIMREF_R_DRIVE_FAULT, .record = true, NOTHEN},
    {.id = "D3", .from = DRIVE_TO_LOCKED, .events = EV(DRIVE_COMMS), .to = SIMREF_ST_LOCKED, .set_reset_seen = T_NO,
     .motor = MOTOR_ZERO, .reason = RT_DRIVE, .record = true, NOTHEN},
    {.id = "D4", .from = DRIVE_STAY, .events = EV(DRIVE_COMMS), STAY, .set_reset_seen = T_NO,
     .reason = RT_DRIVE, .record = true, NOTHEN},
    {.id = "C1", .from = ST(RUN), .events = EV(CMD_STALE), STAY, .motor = MOTOR_ZERO, NOTHEN},
    {.id = "N1", .from = ST(CHARGING), .events = EV(BASE_NOT_STOPPED), .to = SIMREF_ST_LOCKED, .motor = MOTOR_ZERO,
     .reason = SIMREF_R_BASE_NOT_STOPPED, .record = true, NOTHEN},
    {.id = "N2", .from = ST(LOCKED) | ST(RUN) | ST(INHIBITED) | ST(CHARGER_FAULT), .events = EV(BASE_NOT_STOPPED), STAY, NOTHEN},
    {.id = "P1", .from = ST(CHARGER_FAULT), .events = EV(RESET_SHORT), .guard = {GD(CHARGE_CMD_PRESENT, T_NO)}, STAY,
     .then = SIMREF_EV_CHARGER_ACK_INTERNAL},
    {.id = "P2", .from = ST(CHARGER_FAULT), .events = EV(RESET_SHORT), STAY, .result = SIMREF_RES_REJECTED,
     .reason = SIMREF_R_CHARGER_ACK_REJECTED, NOTHEN},
    {.id = "P3", .from = ST(LOCKED), .events = EV(RESET_SHORT), .guard = {GD(ARMING_OK, T_YES), GD(QUALIFIED, T_YES)}, STAY,
     .set_reset_seen = T_YES, .then = SIMREF_EV_RUN_ENTRY_CHECK},
    {.id = "P4", .from = ST(LOCKED), .events = EV(RESET_SHORT), .guard = {GD(ARMING_OK, T_YES), GD(QUALIFIED, T_NO)}, STAY,
     .set_reset_seen = T_NO, .reason = SIMREF_R_QUALIFICATION_FAILED, NOTHEN},
    {.id = "P5", .from = ST(LOCKED), .events = EV(RESET_SHORT), STAY, NOTHEN},
    {.id = "P6", .from = ST(RUN) | ST(INHIBITED) | ST(CHARGING), .events = EV(RESET_SHORT), STAY, NOTHEN},
    {.id = "A1", .from = ST(CHARGER_FAULT), .events = EV(CHARGER_ACK) | EV(CHARGER_ACK_INTERNAL),
     .guard = {GD(CHARGE_CMD_PRESENT, T_NO)}, STAY, .ack = true, .then = SIMREF_EV_CHARGER_FAULT_EXIT},
    {.id = "A2", .from = ST(CHARGER_FAULT), .events = EV(CHARGER_ACK), STAY, .result = SIMREF_RES_REJECTED,
     .reason = SIMREF_R_CHARGER_ACK_REJECTED, NOTHEN},
    {.id = "A3", .from = ST(LOCKED) | ST(RUN) | ST(INHIBITED) | ST(CHARGING), .events = EV(CHARGER_ACK), STAY,
     .result = SIMREF_RES_REJECTED, .reason = SIMREF_R_CHARGER_ACK_REJECTED, NOTHEN},
    {.id = "A4", .from = ST(CHARGER_FAULT), .events = EV(CHARGER_FAULT_EXIT),
     .guard = {GD(WCM_LATCHED, T_NO), GD(INTERLOCK_LATCHED, T_NO)}, .to = SIMREF_ST_LOCKED, NOTHEN},
    {.id = "A5", .from = ST(CHARGER_FAULT), .events = EV(CHARGER_FAULT_EXIT), STAY, NOTHEN},
    {.id = "V1", .from = ST(LOCKED) | ST(INHIBITED) | ST(CHARGING) | ST(CHARGER_FAULT), .events = EV(UI_RESUME),
     .guard = {GD(UI_STOP, T_NO)}, STAY, .result = SIMREF_RES_REJECTED, .reason = SIMREF_R_RESUME_REJECTED, NOTHEN},
    {.id = "V2", .from = ST(INHIBITED), .events = EV(UI_RESUME),
     .guard = {GD(RESET_SEEN, T_YES), GD(JETSON_STOP, T_NO), GD(RUN_OK, T_YES), GD(BASE_STOPPED, T_YES)},
     .set_ui_stop = T_NO, .to = SIMREF_ST_RUN, .run_entry = true, NOTHEN},
#if SIMREF_OPT_C4_RESUME_REJECT_STAYS_INHIBITED
    {.id = "V3", .from = ST(INHIBITED), .events = EV(UI_RESUME), STAY, .result = SIMREF_RES_REJECTED,
     .reason = SIMREF_R_RESUME_REJECTED, NOTHEN},
#else
    /* C4 side b (rev 5 2.3): otherwise LOCKED. Interpretation, not an owner ruling. */
    {.id = "V3", .from = ST(INHIBITED), .events = EV(UI_RESUME), .to = SIMREF_ST_LOCKED, .set_reset_seen = T_NO,
     .motor = MOTOR_ZERO, .result = SIMREF_RES_REJECTED, .reason = SIMREF_R_RESUME_REJECTED, NOTHEN},
#endif
    {.id = "V4", .from = ST(LOCKED) | ST(CHARGING) | ST(CHARGER_FAULT), .events = EV(UI_RESUME), STAY,
     .set_ui_stop = T_NO, NOTHEN},
    {.id = "V5", .from = ST(RUN), .events = EV(UI_RESUME), STAY, .result = SIMREF_RES_REJECTED,
     .reason = SIMREF_R_RESUME_REJECTED, NOTHEN},
    {.id = "L1", .from = ST(LOCKED) | ST(INHIBITED) | ST(CHARGING) | ST(CHARGER_FAULT), .events = EV(JETSON_STOP_RELEASE),
     STAY, .set_jetson_stop = T_NO, NOTHEN},
    {.id = "L2", .from = ST(RUN), .events = EV(JETSON_STOP_RELEASE), STAY, NOTHEN},
    {.id = "G1", .from = ST(LOCKED), .events = EV(CHARGE_CMD), .guard = {GD(CHARGE_PRE_OK, T_YES)},
     .to = SIMREF_ST_CHARGING, .set_reset_seen = T_NO, .var_charge_cmd = T_YES, .result = SIMREF_RES_ACCEPTED, NOTHEN},
    {.id = "G2", .from = ST(LOCKED), .events = EV(CHARGE_CMD), STAY, .result = SIMREF_RES_REJECTED,
     .reason = SIMREF_R_CHARGE_REJECTED, NOTHEN},
    {.id = "G3", .from = ST(RUN) | ST(INHIBITED) | ST(CHARGING) | ST(CHARGER_FAULT), .events = EV(CHARGE_CMD), STAY,
     .result = SIMREF_RES_REJECTED, .reason = SIMREF_R_CHARGE_REJECTED, NOTHEN},
    {.id = "G4", .from = ST(CHARGING), .events = EV(CHARGE_CMD_REMOVED), .to = SIMREF_ST_LOCKED,
     .var_charge_cmd = T_NO, .reason = SIMREF_R_CHARGE_ENDED, .record = true, NOTHEN},
    {.id = "G5", .from = ST(LOCKED) | ST(RUN) | ST(INHIBITED) | ST(CHARGER_FAULT), .events = EV(CHARGE_CMD_REMOVED),
     STAY, .var_charge_cmd = T_NO, NOTHEN},
    {.id = "X1", .from = ST(RUN), .events = EV(MOTION_CMD), .when_queued = T_NO, STAY, .motor = MOTOR_FOLLOW,
     .result = SIMREF_RES_ACCEPTED, NOTHEN},
    {.id = "X2", .from = ALL_ST, .events = EV(MOTION_CMD), STAY, .result = SIMREF_RES_REJECTED,
     .reason = SIMREF_R_MOTION_REJECTED, NOTHEN},
    {.id = "Q1", .from = ST(LOCKED), .events = EV(RUN_ENTRY_CHECK),
     .guard = {GD(RESET_SEEN, T_YES), GD(UI_STOP, T_NO), GD(JETSON_STOP, T_NO), GD(RUN_OK, T_YES), GD(REBOOT_PENDING, T_NO)},
     .to = SIMREF_ST_RUN, .run_entry = true, .q1_extra = true, NOTHEN},
    {.id = "Q2", .from = ALL_ST, .events = EV(RUN_ENTRY_CHECK), STAY, NOTHEN},
};
#define N_ROWS (sizeof ROWS / sizeof ROWS[0])

/* not_applicable (judged on the current state, tick_semantics.applicability) */
static uint32_t not_applicable(simref_event_id_t e)
{
    switch (e) {
#if SIMREF_OPT_C5_INTERLOCK_ONLY_IN_ATTEMPT
    case SIMREF_EV_CHARGE_INTERLOCK: return ST(LOCKED) | ST(RUN) | ST(INHIBITED);
#endif
    case SIMREF_EV_CMD_STALE: return ST(LOCKED) | ST(INHIBITED) | ST(CHARGING) | ST(CHARGER_FAULT);
    default: return 0;
    }
}

static const char *const STATE_NAMES[SIMREF_ST_COUNT] = {"LOCKED", "RUN", "INHIBITED", "CHARGING", "CHARGER_FAULT"};
static const char *const REASON_NAMES[SIMREF_R_COUNT] = {
    "NONE", "BOOT_POWER_ON", "BOOT_WATCHDOG", "BOOT_SOFTWARE", "UI_STOP", "HW_SAFETY_NOT_ENABLED",
    "ESTOP_DIAG", "COMMS_LOST", "BMS_FAULT", "INPUT_INVALID:MISSING", "INPUT_INVALID:STALE",
    "INPUT_INVALID:IMPLAUSIBLE", "INPUT_INVALID:RESET_HIGH", "REBOOT_REQUESTED", "CHARGER_FAULT",
    "CHARGE_INTERLOCK", "DRIVE_FAULT", "DRIVE_COMMS", "DRIVE_CONFIG", "CHARGE_ENDED", "SESSION_OLD",
    "QUALIFICATION_FAILED", "RESUME_REJECTED", "SEQ_REJECTED", "CHARGE_REJECTED",
    "CHARGER_ACK_REJECTED", "BASE_NOT_STOPPED", "MOTION_REJECTED"};
static const char *const EVENT_NAMES[SIMREF_EV_COUNT] = {
    "boot", "ui_stop", "safety_ok_low", "estop_diag", "session_lost", "bms_fault", "input_invalid",
    "reset_hold_5s", "wcm_fault", "charge_interlock", "drive_fault", "drive_comms", "cmd_stale",
    "base_not_stopped", "reset_short", "ui_resume", "jetson_stop_release", "charge_cmd",
    "charge_cmd_removed", "charger_ack", "motion_cmd", "wcm_fault_clear", "charger_ack_internal",
    "charger_fault_exit", "run_entry_check"};

const char *simref_state_name(simref_state_t s) { return (unsigned)s < SIMREF_ST_COUNT ? STATE_NAMES[s] : "?"; }
const char *simref_reason_name(simref_reason_t r) { return (unsigned)r < SIMREF_R_COUNT ? REASON_NAMES[r] : "?"; }
const char *simref_event_name(simref_event_id_t e) { return (unsigned)e < SIMREF_EV_COUNT ? EVENT_NAMES[e] : "?"; }
size_t simref_sm_row_count(void) { return N_ROWS; }
const char *simref_sm_row_id(size_t i) { return i < N_ROWS ? ROWS[i].id : NULL; }
int simref_sm_row_to(size_t i) { return i < N_ROWS ? ROWS[i].to : -2; }
uint32_t simref_sm_row_from_mask(size_t i) { return i < N_ROWS ? ROWS[i].from : 0u; }
uint32_t simref_sm_row_event_mask(size_t i) { return i < N_ROWS ? ROWS[i].events : 0u; }

/* priority: [reset_hold_5s, wcm_fault, charge_interlock, others] */
int simref_event_rank(simref_event_id_t e)
{
    switch (e) {
    case SIMREF_EV_RESET_HOLD_5S: return 0;
    case SIMREF_EV_WCM_FAULT: return 1;
    case SIMREF_EV_CHARGE_INTERLOCK: return 2;
    default: return 3;
    }
}

bool simref_sm_lamp_on(simref_state_t s)
{
    return s == SIMREF_ST_LOCKED || s == SIMREF_ST_CHARGING || s == SIMREF_ST_CHARGER_FAULT;
}

bool simref_sm_charge_request_on(simref_state_t s) { return s == SIMREF_ST_CHARGING; }

void simref_sm_init(simref_sm_t *sm)
{
    memset(sm, 0, sizeof *sm);
    sm->state = SIMREF_ST_LOCKED;
    sm->latches.jetson_stop = true;  /* boot: set */
    sm->first_reason = SIMREF_R_NONE;
    sm->wcm_blocks_run = SIMREF_OPT_C3_WCM_BLOCKS_RUN != 0;
}

static bool guard_value(const simref_sm_t *sm, int key)
{
    switch (key) {
    case G_CHARGE_CMD_PRESENT: return sm->charge_cmd_present;
    case G_ARMING_OK: return sm->inputs.arming_ok;
    case G_QUALIFIED: return sm->inputs.qualified;
    case G_UI_STOP: return sm->latches.ui_stop;
    case G_RESET_SEEN: return sm->latches.reset_seen;
    case G_JETSON_STOP: return sm->latches.jetson_stop;
    case G_RUN_OK: return sm->inputs.run_ok;
    case G_BASE_STOPPED: return sm->inputs.base_stopped;
    case G_CHARGE_PRE_OK: return sm->inputs.charge_pre_ok;
    case G_WCM_LATCHED: return sm->latches.wcm_latched;
    case G_INTERLOCK_LATCHED: return sm->latches.interlock_latched;
    case G_REBOOT_PENDING: return sm->reboot_pending;
    default: return false;
    }
}

static bool tri_ok(uint8_t want, bool have) { return want == T_ANY || (want == T_YES) == have; }

static bool row_matches(const simref_sm_t *sm, const row_t *r, const simref_event_t *ev)
{
    if (!(r->events & (1u << ev->id)) || !(r->from & (1u << sm->state)))
        return false;
    if (!tri_ok(r->when_session_old, ev->session_old) || !tri_ok(r->when_seq_replay, ev->seq_replay) ||
        !tri_ok(r->when_queued, ev->queued))
        return false;
    for (int k = 0; k < G_COUNT; k++)
        if (!tri_ok(r->guard[k], guard_value(sm, k)))
            return false;
    if (r->q1_extra && sm->wcm_blocks_run && sm->wcm_active)
        return false; /* CONFLICT C3 side b. Interpretation, not an owner ruling. */
    return true;
}

static void tri_apply(bool *dst, uint8_t v)
{
    if (v != T_ANY)
        *dst = (v == T_YES);
}

static bool push_reason(simref_sm_t *sm, simref_reason_t r)
{
    if (sm->n_reasons >= SIMREF_SM_MAX_TICK_OUT) {
        sm->overflow = true;
        return false;
    }
    sm->reasons[sm->n_reasons++] = r;
    return true;
}

static bool is_active(simref_state_t s)
{
    return s == SIMREF_ST_RUN || s == SIMREF_ST_INHIBITED || s == SIMREF_ST_CHARGING;
}

static bool is_lock(simref_state_t s) { return s == SIMREF_ST_LOCKED || s == SIMREF_ST_CHARGER_FAULT; }

static void episode_clear(simref_sm_t *sm)
{
    sm->n_mask = 0;
    sm->first_reason = SIMREF_R_NONE;
}

static void record(simref_sm_t *sm, simref_reason_t r, simref_state_t prev)
{
    if (is_active(prev) && is_lock(sm->state))
        episode_clear(sm);
    bool present = false;
    for (unsigned i = 0; i < sm->n_mask; i++)
        present = present || sm->reason_mask[i] == r;
    if (!present && sm->n_mask < SIMREF_R_COUNT)
        sm->reason_mask[sm->n_mask++] = (uint8_t)r;
    if (sm->first_reason == SIMREF_R_NONE)
        sm->first_reason = r;
}

static bool ack(simref_sm_t *sm)
{
    bool cleared = false;
    if (sm->latches.wcm_latched && !sm->wcm_active) {
        sm->latches.wcm_latched = false;
        cleared = true;
    }
    if (sm->latches.interlock_latched) {
        sm->latches.interlock_latched = false;
        cleared = true;
    }
    return cleared;
}

static simref_reason_t reason_of(int8_t rt, const simref_event_t *ev)
{
    switch (rt) {
    case RT_BOOT: return (simref_reason_t)(SIMREF_R_BOOT_POWER_ON + (ev->cause % 3u));
    case RT_INPUT: return (simref_reason_t)(SIMREF_R_INPUT_INVALID_MISSING + (ev->subcause % 4u));
    case RT_DRIVE: return ev->kind == SIMREF_KIND_CONFIG ? SIMREF_R_DRIVE_CONFIG : SIMREF_R_DRIVE_COMMS;
    default: return (simref_reason_t)rt;
    }
}


static simref_sm_err_t apply_depth(simref_sm_t *sm, const simref_event_t *ev, int depth, simref_state_t applic_state)
{

    if (depth > 4)
        return SIMREF_SM_E_LOOP;
    if ((unsigned)ev->id >= SIMREF_EV_COUNT)
        return SIMREF_SM_E_ARG;
    if (not_applicable(ev->id) & (1u << applic_state))
        return SIMREF_SM_E_NOT_APPLICABLE;
    const row_t *row = NULL;
    size_t idx = 0;
    for (size_t i = 0; i < N_ROWS && row == NULL; i++)
        if (row_matches(sm, &ROWS[i], ev)) {
            row = &ROWS[i];
            idx = i;
        }
    if (row == NULL)
        return SIMREF_SM_E_NO_ROW;
    if (sm->n_rows < SIMREF_SM_MAX_TICK_OUT)
        sm->rows[sm->n_rows++] = (uint8_t)idx;
    simref_state_t prev = sm->state;
    tri_apply(&sm->latches.reset_seen, row->set_reset_seen);
    tri_apply(&sm->latches.ui_stop, row->set_ui_stop);
    tri_apply(&sm->latches.jetson_stop, row->set_jetson_stop);
    tri_apply(&sm->latches.wcm_latched, row->set_wcm_latched);
    tri_apply(&sm->latches.interlock_latched, row->set_interlock_latched);
    tri_apply(&sm->charge_cmd_present, row->var_charge_cmd);
    tri_apply(&sm->wcm_active, row->var_wcm_active);
    tri_apply(&sm->reboot_pending, row->var_reboot_pending);
    if (ev->id == SIMREF_EV_BOOT)
        episode_clear(sm);
    if (row->to >= 0)
        sm->state = (simref_state_t)row->to;
    if (row->motor == MOTOR_ZERO)
        sm->motor_follow = false;
    else if (row->motor == MOTOR_FOLLOW)
        sm->motor_follow = true;
    if (sm->state != SIMREF_ST_RUN)
        sm->motor_follow = false;
    if (row->run_entry || (sm->state == SIMREF_ST_RUN && prev != SIMREF_ST_RUN)) {
        sm->motor_follow = false;
        episode_clear(sm); /* lock episode ends (4.1) */
    }
    uint8_t result = row->result;
    if (row->ack) {
        result = ack(sm) ? SIMREF_RES_ACCEPTED : SIMREF_RES_REJECTED;
        if (result == SIMREF_RES_REJECTED && !push_reason(sm, SIMREF_R_CHARGER_ACK_REJECTED))
            return SIMREF_SM_E_OVERFLOW;
    }
    if (result) {
        if (sm->n_results >= SIMREF_SM_MAX_TICK_OUT) {
            sm->overflow = true;
            return SIMREF_SM_E_OVERFLOW;
        }
        sm->results[sm->n_results++] = (simref_result_t)result;
    }
    if (row->reason) {
        simref_reason_t r = reason_of(row->reason, ev);
        if (!push_reason(sm, r))
            return SIMREF_SM_E_OVERFLOW;
        if (row->record)
            record(sm, r, prev);
    }
    if (row->then >= 0) {
        simref_event_t f = {.id = (simref_event_id_t)row->then};
        return apply_depth(sm, &f, depth + 1, sm->state);
    }
    return SIMREF_SM_OK;
}

simref_sm_err_t simref_sm_apply(simref_sm_t *sm, const simref_event_t *ev)
{
    if (sm == NULL || ev == NULL)
        return SIMREF_SM_E_ARG;
    return apply_depth(sm, ev, 0, sm->state);
}

simref_sm_err_t simref_sm_tick(simref_sm_t *sm, const simref_sm_inputs_t *in, const simref_event_t *ev, size_t n_ev)
{
    if (sm == NULL || (n_ev > 0 && ev == NULL) || n_ev > SIMREF_SM_MAX_TICK_EVENTS)
        return SIMREF_SM_E_ARG;
    if (in != NULL)
        sm->inputs = *in;
    sm->n_reasons = sm->n_results = sm->n_rows = 0;
    sm->overflow = false;
    const simref_state_t tick_start = sm->state;
    /* order: stable sort by priority rank (insertion sort keeps arrival order in a rank) */
    size_t order[SIMREF_SM_MAX_TICK_EVENTS];
    for (size_t i = 0; i < n_ev; i++) {
        size_t j = i;
        int ri = simref_event_rank(ev[i].id);
        while (j > 0) {
            int rp = simref_event_rank(ev[order[j - 1]].id);
#if SIMREF_OPT_TICK_ORDER_STABLE_OTHERS
            bool before = rp > ri;
#else
            bool before = rp > ri || (rp == ri && ri == 3 && ev[order[j - 1]].id > ev[i].id);
#endif
            if (!before)
                break;
            order[j] = order[j - 1];
            j--;
        }
        order[j] = i;
    }
    for (size_t k = 0; k < n_ev; k++) {
#if SIMREF_OPT_TICK_APPLICABILITY_CURRENT_STATE
        simref_state_t applic = sm->state;
#else
        simref_state_t applic = tick_start; /* version 1 reading. Interpretation, not an owner ruling. */
#endif
        (void)tick_start;
        simref_sm_err_t e = apply_depth(sm, &ev[order[k]], 0, applic);
        if (e != SIMREF_SM_OK)
            return e;
    }
    return SIMREF_SM_OK;
}

bool simref_sm_motion_admissible(const simref_sm_t *sm)
{
    simref_sm_t probe = *sm;
    probe.n_reasons = probe.n_results = probe.n_rows = 0;
    simref_event_t m = {.id = SIMREF_EV_MOTION_CMD};
    if (simref_sm_apply(&probe, &m) != SIMREF_SM_OK)
        return false;
    return probe.motor_follow;
}
