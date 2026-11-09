/* SPDX-License-Identifier: MIT */
/*
 * E-01 safety state machine rev 3 (platform-fw #17), portable core.
 *
 * PROPOSAL for #17 (Evans to adopt or rewrite). Per the repository rules this
 * module is human-authored and needs two human approvals including the
 * platform lead. It is a secondary, observe-and-inhibit layer: the hardwired
 * E-stop chain (PNOZ s3, K1/K2) is the safety function; firmware is never the
 * only path to a safe state.
 *
 * Structure (rev 3): plain C, a struct of timestamped inputs in, a struct of
 * outputs plus state and reasons out, no GPIO or HAL. The board port reads the
 * pins and applies polarity; this core applies the debounce and the staleness
 * rules so that the host tests can drive them.
 *
 * States: LOCKED (boot and any fault; motion inhibited; lamp on), RUN (motion
 * allowed; lamp off), CHARGING (motion inhibited; lamp on; CHARGE_REQUEST on),
 * CHARGER_FAULT (latched; motion inhibited; lamp on).
 *
 * Interpretations of rev 3 made here (for Evans to confirm):
 * - RESET is filtered on both edges (20 ms), because a press arms the reset;
 *   all other inputs act at once when going unsafe and need 20 ms when going safe.
 * - The reset-response window is confirmed when SAFETY_OK is high (stable) at
 *   any time within 1 s after the short press is released.
 * - "Software Stop asserted" and "SAFETY_OK high-to-low" clear reset_seen on
 *   the edge; the other clear conditions act while they are present.
 * - A charger-fault acknowledgement (short press in CHARGER_FAULT) only counts
 *   when, at that moment, the charge command is removed and, for a charger
 *   fault, WCM_FAULT is normal.
 * - CHARGING ends on any fault, including stale or moving encoders.
 */
#ifndef OAMR_SAFETY_CORE_H
#define OAMR_SAFETY_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OAMR_SC_LOCKED = 0,
    OAMR_SC_RUN,
    OAMR_SC_CHARGING,
    OAMR_SC_CHARGER_FAULT
} oamr_sc_state_t;

typedef enum {
    OAMR_SC_COLUMN_UNKNOWN = 0,
    OAMR_SC_COLUMN_RELEASED,
    OAMR_SC_COLUMN_PRESSED
} oamr_sc_column_estop_t;

/* Reason bits (rev 3 status list). */
enum {
    OAMR_SC_R_ESTOP_DIAG = 1u << 0,             /* E-stop diagnostic active or stale */
    OAMR_SC_R_HW_SAFETY_NOT_ENABLED = 1u << 1,  /* SAFETY_OK (PNOZ Y32) low */
    OAMR_SC_R_CHARGER_FAULT = 1u << 2,
    OAMR_SC_R_CHARGE_INTERLOCK = 1u << 3,
    OAMR_SC_R_RESET_IN_PROGRESS = 1u << 4,
    OAMR_SC_R_PNOZ_DID_NOT_ENABLE = 1u << 5,
    OAMR_SC_R_REBOOT_REQUESTED = 1u << 6,
    OAMR_SC_R_BMS_FAULT = 1u << 7,              /* BMS fault or stale */
    OAMR_SC_R_COMMS_LOST = 1u << 8,             /* Jetson comms not fresh */
    OAMR_SC_R_INPUT_INVALID = 1u << 9,
    OAMR_SC_R_SOFTWARE_STOP = 1u << 10
};

/* Latch kinds in CHARGER_FAULT. */
enum {
    OAMR_SC_LATCH_CHARGER = 1u << 0,            /* WCM_FAULT while charging */
    OAMR_SC_LATCH_INTERLOCK = 1u << 1           /* SAFETY_OK high with CHARGE_REQUEST on */
};

typedef struct {
    uint64_t debounce_us;              /* 20 ms */
    uint64_t short_press_max_us;       /* under 2 s = short press */
    uint64_t long_press_us;            /* 5 s or more = reboot request */
    uint64_t reset_window_us;          /* 1 s reset-response window */
    uint64_t controller_reset_delay_us;/* 2 s after the reboot request */
    uint64_t charge_interlock_us;      /* 500 ms: SAFETY_OK must be low by then */
    uint64_t comms_timeout_us;         /* 500 ms host heartbeat (I8 WP) */
    uint64_t bms_stale_us;             /* TODO(Evans): 3 s (P-02 rev19.2) */
    uint64_t column_estop_stale_us;    /* TODO(Evans): CTL-017 status timeout */
    uint64_t encoder_stale_us;         /* TODO(Evans) */
    float stopped_rpm;                 /* 1 rpm on both wheels */
    uint64_t stopped_hold_us;          /* for 500 ms */
} oamr_sc_config_t;

/* Inputs for one tick. Direct inputs are polarity-corrected by the board port
 * (true = the named condition is present). */
typedef struct {
    bool gpio_valid;                   /* complete, fresh sample of the direct inputs */
    bool safety_ok;                    /* PNOZ Y32 high = enabled */
    bool estop_base_active;            /* base E-stop spare NO: pressed */
    bool reset_pressed;                /* RESET NC open (open wire reads as held) */
    bool wcm_fault;                    /* TZBOT charger fault asserted */
    oamr_sc_column_estop_t estop_column; /* second E-stop via CTL-017 (CAN3) */
    bool estop_column_seen;
    uint64_t estop_column_rx_us;
    bool sw_stop;                      /* Jetson software Stop (asserted at boot) */
    bool charge_cmd;                   /* Jetson charge command (level; accepted on its rising edge) */
    bool host_seen;
    uint64_t host_rx_us;               /* last Jetson heartbeat */
    bool bms_seen;
    bool bms_fault;
    uint64_t bms_rx_us;
    bool encoder_seen;
    float encoder_left_rpm;
    float encoder_right_rpm;
    uint64_t encoder_rx_us;
} oamr_sc_inputs_t;

typedef struct {
    oamr_sc_state_t state;
    bool motion_allowed;
    bool reset_lamp;
    bool charge_request;
    bool reboot_request;               /* to the Jetson; stays set until the controller resets */
    bool controller_reset;             /* the port must reset the MCU now */
    uint32_t reasons;                  /* OAMR_SC_R_* currently present */
    uint32_t first_reason;             /* single bit, kept until RUN */
    uint32_t transition_reason;        /* reason of the last transition out of RUN or CHARGING */
    uint32_t latches;                  /* OAMR_SC_LATCH_* */
    bool reset_seen;
    bool reset_window_open;
} oamr_sc_outputs_t;

typedef struct {
    bool value;
    bool pending;
    uint64_t pending_since_us;
    uint64_t changed_us;
} oamr_sc_debounce_t;

typedef struct {
    oamr_sc_config_t cfg;
    oamr_sc_outputs_t out;
    bool inputs_complete;              /* a full valid sample has been seen since boot */
    oamr_sc_debounce_t safety_ok;
    oamr_sc_debounce_t estop_base;
    oamr_sc_debounce_t reset;
    oamr_sc_debounce_t wcm;
    bool prev_safety_ok;
    bool prev_sw_stop;
    bool prev_charge_cmd;
    bool prev_reset;
    bool reset_released_seen;          /* RESET seen released since boot */
    bool press_active;
    bool long_press_fired;
    uint64_t press_start_us;
    uint64_t window_start_us;
    bool stopped_tracking;
    uint64_t stopped_since_us;
    uint64_t charge_on_us;
    uint64_t controller_reset_at_us;
    bool pnoz_did_not_enable;
    bool first_tick_done;
} oamr_sc_t;

void oamr_sc_default_config(oamr_sc_config_t *cfg);
void oamr_sc_init(oamr_sc_t *sc, const oamr_sc_config_t *cfg);
const oamr_sc_outputs_t *oamr_sc_step(oamr_sc_t *sc, const oamr_sc_inputs_t *in, uint64_t now_us);
static inline bool oamr_sc_motion_allowed(const oamr_sc_t *sc)
{
    return sc->out.motion_allowed;
}

#ifdef __cplusplus
}
#endif

#endif /* OAMR_SAFETY_CORE_H */
