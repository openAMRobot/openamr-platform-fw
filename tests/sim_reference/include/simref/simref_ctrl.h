/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
/*
 * The base controller role of the simulation reference: state machine, command watchdog,
 * drive link supervision, CiA 402 enable sequence and wheel targets, charge request logic and
 * the lift permit producer, driven by simulated pins, host messages and CAN frames. No OS
 * calls; the Linux process (simref_main.c) supplies time, sockets and CAN.
 */
#ifndef SIMREF_CTRL_H
#define SIMREF_CTRL_H

#include <stdbool.h>
#include <stdint.h>

#include "simref/simref_can.h"
#include "simref/simref_drive.h"
#include "simref/simref_permit.h"
#include "simref/simref_sm.h"

typedef enum {
    SIMREF_PIN_Y32 = 0,        /* SAFETY_OK from the hardwired chain, high = enabled */
    SIMREF_PIN_ESTOP_CONTACT,  /* base E-stop spare NO contact, 1 = E-stop pressed */
    SIMREF_PIN_RESET,          /* RESET button, 1 = pressed */
    SIMREF_PIN_CHARGER_FAULT,  /* WCM_FAULT, 1 = asserted */
    SIMREF_PIN_CONTACTOR_FB,   /* contactor feedback, 1 = closed */
    SIMREF_PIN_COUNT
} simref_pin_t;

typedef enum {
    SIMREF_SUP_UI_STOP = 0, SIMREF_SUP_UI_RESUME, SIMREF_SUP_JETSON_STOP_RELEASE, SIMREF_SUP_CHARGE_CMD,
    SIMREF_SUP_CHARGE_CMD_REMOVED, SIMREF_SUP_CHARGER_ACK, SIMREF_SUP_COUNT
} simref_sup_t;

typedef enum { SIMREF_INPUT_DETAIL_NONE = 0, SIMREF_INPUT_DETAIL_CONTACTOR_FEEDBACK, SIMREF_INPUT_DETAIL_ENCODER } simref_input_detail_t;

typedef struct {
    uint8_t drive_node, lift_node, core_node;
    double wheel_radius_m, wheel_separation_m;
    int left_sign, right_sign;
    simref_cause_t boot_cause;
    bool bms_ok;              /* the reference does not poll the BMS in this mode */
    simref_permit_cfg_t permit;
} simref_ctrl_cfg_t;

void simref_ctrl_default_cfg(simref_ctrl_cfg_t *cfg);

#define SIMREF_CTRL_QUEUE 32
#define SIMREF_CTRL_LOG 16

typedef struct {
    int64_t t_us;
    simref_reason_t reason;
} simref_reason_log_t;

typedef struct {
    simref_ctrl_cfg_t cfg;
    simref_send_fn send;
    void *send_ctx;
    simref_sm_t sm;
    simref_drive_t drive;
    simref_permit_t permit;
    simref_permit_tx_t permit_tx;
    int64_t boot_us;
    /* pending abstract events for the next tick */
    simref_event_t q[SIMREF_CTRL_QUEUE];
    int nq;
    bool q_overflow;
    int64_t next_tick_us;
    simref_sm_err_t last_err;
    uint32_t spec_errors;
    /* pins */
    bool pins_known;
    unsigned pins_seen;
    bool release_pending;
    uint32_t wheel_epoch;
    bool raw[SIMREF_PIN_COUNT];
    bool deb[SIMREF_PIN_COUNT];
    int64_t raw_since_us[SIMREF_PIN_COUNT];
    bool reset_low_seen;
    bool reset_pressed;
    int64_t reset_press_us;
    bool reset_invalid_sent, reset_reboot_sent;
    bool contactor_mismatch, contactor_reported;
    int64_t contactor_mismatch_since_us;
    simref_input_detail_t input_detail;
    /* charge */
    bool charging_prev;
    int64_t charge_request_on_us;
    bool interlock_window_checked, interlock_reported;
    int64_t y32_high_since_us;
    bool base_not_stopped_reported;
    /* host session */
    bool session_up;
    uint32_t session_epoch;
    int64_t session_hb_us;
    bool seq_has[SIMREF_SUP_COUNT + 1];
    uint32_t seq_last[SIMREF_SUP_COUNT + 1];
    /* motion command */
    double cmd_v, cmd_w;
    bool cmd_valid_seen;
    int64_t cmd_us;
    bool stale_reported;
    int64_t run_entry_us;
    uint32_t cmd_accepted, cmd_rejected;
    /* base stopped (C2) */
    bool stopped_raw;
    int64_t stopped_since_us;
    /* odometry from 606Ch */
    double x, y, yaw, v, w, wheel_pos[2], wheel_rad_s[2];
    int64_t odom_us;
    bool odom_valid;
    uint32_t odom_seq_seen[2];
    /* lift bus */
    int64_t core_hb_us, lift_ka_us, permit_tx_us, lift_nmt_us;
    bool lift_hb_seen;
    int64_t lift_hb_us;
    uint8_t lift_nmt;
    bool lift_cmd_valid;
    uint8_t lift_cmd[3];
    uint8_t lift_seq;
    uint8_t lift_status[8];
    bool lift_status_valid;
    /* arms evidence */
    int64_t arms_seq;
    /* status */
    simref_reason_log_t log[SIMREF_CTRL_LOG];
    uint32_t log_n;
    int32_t target_rpm[2];
    uint32_t reboots;
} simref_ctrl_t;

void simref_ctrl_init(simref_ctrl_t *c, const simref_ctrl_cfg_t *cfg, int64_t now_us, simref_send_fn send, void *ctx);
void simref_ctrl_set_pin(simref_ctrl_t *c, simref_pin_t pin, bool level, int64_t now_us);
void simref_ctrl_on_frame(simref_ctrl_t *c, simref_bus_t bus, const simref_frame_t *f, int64_t now_us);
void simref_ctrl_host_session(simref_ctrl_t *c, uint32_t epoch, int64_t now_us);
void simref_ctrl_host_heartbeat(simref_ctrl_t *c, uint32_t epoch, int64_t now_us);
void simref_ctrl_host_cmd(simref_ctrl_t *c, uint32_t epoch, uint32_t seq, double v, double w, int64_t now_us);
void simref_ctrl_host_supervisory(simref_ctrl_t *c, simref_sup_t kind, uint32_t epoch, uint32_t seq, int64_t now_us);
void simref_ctrl_host_lift(simref_ctrl_t *c, uint8_t op, uint8_t stop, int64_t now_us);
void simref_ctrl_host_arms(simref_ctrl_t *c, const simref_arm_t *l, const simref_arm_t *r, simref_pose_t bimanual,
                           int64_t now_us);
/* Runs everything that is due at now_us. */
void simref_ctrl_step(simref_ctrl_t *c, int64_t now_us);
int64_t simref_ctrl_next_deadline(const simref_ctrl_t *c);
/* Aggregate condition inputs as computed now (exposed for tests and status). */
simref_sm_inputs_t simref_ctrl_inputs(const simref_ctrl_t *c, int64_t now_us);

#endif
