/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
/*
 * Operating state machine of docs/safety/spec/state_machine.yaml version 2, as a C table.
 * Abstract events in, state, latches, outputs and reasons out. No I/O, no global state.
 */
#ifndef SIMREF_SM_H
#define SIMREF_SM_H

#include "simref/simref_host_only.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SIMREF_SM_SPEC_VERSION 2

typedef enum {
    SIMREF_ST_LOCKED = 0,
    SIMREF_ST_RUN,
    SIMREF_ST_INHIBITED,
    SIMREF_ST_CHARGING,
    SIMREF_ST_CHARGER_FAULT,
    SIMREF_ST_COUNT
} simref_state_t;

typedef enum {
    SIMREF_EV_BOOT = 0,
    SIMREF_EV_UI_STOP,
    SIMREF_EV_SAFETY_OK_LOW,
    SIMREF_EV_ESTOP_DIAG,
    SIMREF_EV_SESSION_LOST,
    SIMREF_EV_BMS_FAULT,
    SIMREF_EV_INPUT_INVALID,
    SIMREF_EV_RESET_HOLD_5S,
    SIMREF_EV_WCM_FAULT,
    SIMREF_EV_CHARGE_INTERLOCK,
    SIMREF_EV_DRIVE_FAULT,
    SIMREF_EV_DRIVE_COMMS,
    SIMREF_EV_CMD_STALE,
    SIMREF_EV_BASE_NOT_STOPPED,
    SIMREF_EV_RESET_SHORT,
    SIMREF_EV_UI_RESUME,
    SIMREF_EV_JETSON_STOP_RELEASE,
    SIMREF_EV_CHARGE_CMD,
    SIMREF_EV_CHARGE_CMD_REMOVED,
    SIMREF_EV_CHARGER_ACK,
    SIMREF_EV_MOTION_CMD,
    SIMREF_EV_WCM_FAULT_CLEAR,
    SIMREF_EV_CHARGER_ACK_INTERNAL,
    SIMREF_EV_CHARGER_FAULT_EXIT,
    SIMREF_EV_RUN_ENTRY_CHECK,
    SIMREF_EV_COUNT
} simref_event_id_t;

/* Event parameters. Unused fields are ignored. */
typedef enum { SIMREF_CAUSE_POWER_ON = 0, SIMREF_CAUSE_WATCHDOG, SIMREF_CAUSE_SOFTWARE } simref_cause_t;
typedef enum { SIMREF_SUB_MISSING = 0, SIMREF_SUB_STALE, SIMREF_SUB_IMPLAUSIBLE, SIMREF_SUB_RESET_HIGH } simref_subcause_t;
typedef enum { SIMREF_KIND_COMMS = 0, SIMREF_KIND_CONFIG } simref_kind_t;

typedef struct {
    simref_event_id_t id;
    uint8_t cause;      /* simref_cause_t */
    uint8_t subcause;   /* simref_subcause_t */
    uint8_t kind;       /* simref_kind_t */
    bool session_old;   /* session: old (default current) */
    bool seq_replay;    /* seq: replay (default new) */
    bool queued;        /* motion_cmd queued before the latest RUN entry */
} simref_event_t;

/* Condition inputs (levels, kept until changed). Missing reads as not satisfied. */
typedef struct {
    bool run_ok;
    bool base_stopped;
    bool arming_ok;
    bool qualified;
    bool charge_pre_ok;
} simref_sm_inputs_t;

typedef enum {
    SIMREF_R_NONE = 0,
    SIMREF_R_BOOT_POWER_ON, SIMREF_R_BOOT_WATCHDOG, SIMREF_R_BOOT_SOFTWARE,
    SIMREF_R_UI_STOP, SIMREF_R_HW_SAFETY_NOT_ENABLED, SIMREF_R_ESTOP_DIAG, SIMREF_R_COMMS_LOST,
    SIMREF_R_BMS_FAULT,
    SIMREF_R_INPUT_INVALID_MISSING, SIMREF_R_INPUT_INVALID_STALE,
    SIMREF_R_INPUT_INVALID_IMPLAUSIBLE, SIMREF_R_INPUT_INVALID_RESET_HIGH,
    SIMREF_R_REBOOT_REQUESTED, SIMREF_R_CHARGER_FAULT, SIMREF_R_CHARGE_INTERLOCK,
    SIMREF_R_DRIVE_FAULT, SIMREF_R_DRIVE_COMMS, SIMREF_R_DRIVE_CONFIG, SIMREF_R_CHARGE_ENDED,
    SIMREF_R_SESSION_OLD,
    /* ADDED placeholders (state_machine.yaml reason_codes.added) */
    SIMREF_R_QUALIFICATION_FAILED, SIMREF_R_RESUME_REJECTED, SIMREF_R_SEQ_REJECTED,
    SIMREF_R_CHARGE_REJECTED, SIMREF_R_CHARGER_ACK_REJECTED, SIMREF_R_BASE_NOT_STOPPED,
    SIMREF_R_MOTION_REJECTED,
    SIMREF_R_COUNT
} simref_reason_t;

typedef enum { SIMREF_RES_ACCEPTED = 1, SIMREF_RES_REJECTED = 2 } simref_result_t;

typedef enum {
    SIMREF_SM_OK = 0,
    SIMREF_SM_E_NOT_APPLICABLE,   /* spec error: event not applicable in the current state */
    SIMREF_SM_E_NO_ROW,           /* spec error: no transition row matched */
    SIMREF_SM_E_LOOP,             /* internal event loop */
    SIMREF_SM_E_OVERFLOW,         /* more reasons or results than the buffers hold */
    SIMREF_SM_E_ARG
} simref_sm_err_t;

#define SIMREF_SM_MAX_TICK_EVENTS 32
#define SIMREF_SM_MAX_TICK_OUT 48

typedef struct {
    bool reset_seen, ui_stop, jetson_stop, wcm_latched, interlock_latched;
} simref_latches_t;

typedef struct {
    simref_state_t state;
    simref_latches_t latches;
    bool charge_cmd_present, wcm_active, reboot_pending;
    bool motor_follow;            /* motor output FOLLOW_CMD (else ZERO) */
    simref_sm_inputs_t inputs;
    /* lock episode (rev 5 4.1) */
    uint8_t reason_mask[SIMREF_R_COUNT];
    uint8_t n_mask;
    simref_reason_t first_reason;
    /* outputs of the last tick */
    simref_reason_t reasons[SIMREF_SM_MAX_TICK_OUT];
    uint8_t n_reasons;
    simref_result_t results[SIMREF_SM_MAX_TICK_OUT];
    uint8_t n_results;
    uint8_t rows[SIMREF_SM_MAX_TICK_OUT];   /* row indices fired in the last tick */
    uint8_t n_rows;
    bool overflow;
    bool wcm_blocks_run;          /* C3, from SIMREF_OPT_C3_WCM_BLOCKS_RUN */
} simref_sm_t;

void simref_sm_init(simref_sm_t *sm);
/* One control tick. Inputs are set once before the first event; events are applied in the
 * priority order of the specification. Returns SIMREF_SM_OK or a spec error; on error the
 * events after the failing one are not applied. */
simref_sm_err_t simref_sm_tick(simref_sm_t *sm, const simref_sm_inputs_t *in,
                               const simref_event_t *ev, size_t n_ev);
/* One event, no reset of the tick outputs (used by the tick and the probe). */
simref_sm_err_t simref_sm_apply(simref_sm_t *sm, const simref_event_t *ev);
/* Would a fresh, current-session, new-sequence motion command be executed now? (probe on a copy) */
bool simref_sm_motion_admissible(const simref_sm_t *sm);

bool simref_sm_lamp_on(simref_state_t s);
bool simref_sm_charge_request_on(simref_state_t s);
const char *simref_state_name(simref_state_t s);
const char *simref_reason_name(simref_reason_t r);
const char *simref_event_name(simref_event_id_t e);
/* Introspection for the structural cross-check against the YAML table. */
size_t simref_sm_row_count(void);
const char *simref_sm_row_id(size_t i);
int simref_sm_row_to(size_t i);                  /* -1 = stay */
uint32_t simref_sm_row_from_mask(size_t i);      /* bit per state */
uint32_t simref_sm_row_event_mask(size_t i);     /* bit per event */
int simref_event_rank(simref_event_id_t e);      /* priority rank, lower first */

#endif
