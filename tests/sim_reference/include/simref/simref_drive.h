/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
/*
 * CAN1 drive link to ZLAC8015D node 1: heartbeat consumer (drive link supervision), boot
 * reads, CiA 402 enable sequence and cyclic wheel targets, all by expedited SDO, following
 * docs/architecture/can1-drive-link.md sections 5.1 and 5.3 (vectors R1 to R7, E1 to E6).
 */
#ifndef SIMREF_DRIVE_H
#define SIMREF_DRIVE_H

#include <stdbool.h>
#include <stdint.h>

#include "simref/simref_can.h"

typedef enum {
    SIMREF_DL_ABSENT = 0,  /* no heartbeat seen since start */
    SIMREF_DL_BOOT_READ,   /* R2 to R7 */
    SIMREF_DL_IDLE,        /* disabled, boot read passed, waits for an enable request */
    SIMREF_DL_ENABLING,    /* E1 to E6 */
    SIMREF_DL_ENABLED,     /* operation enabled, cyclic targets */
    SIMREF_DL_FAILED,      /* disabled after a failure, waits for an enable request */
    SIMREF_DL_LOST,        /* heartbeat lost: nothing is sent on CAN1 */
    SIMREF_DL_COUNT
} simref_dl_phase_t;

typedef enum {
    SIMREF_DD_NONE = 0,
    SIMREF_DD_HB_TIMEOUT,
    SIMREF_DD_DRIVE_RESTARTED,
    SIMREF_DD_SDO_TIMEOUT,
    SIMREF_DD_SDO_ABORT,
    SIMREF_DD_TRANSITION_TIMEOUT,
    SIMREF_DD_CONFIG_MISMATCH,
    SIMREF_DD_DRIVE_FAULT,
    SIMREF_DD_NOT_ENABLED,
    SIMREF_DD_FOUND_ENABLED_AT_BOOT,
    SIMREF_DD_ENCODER_READ_FAILED,
    SIMREF_DD_COUNT
} simref_dl_detail_t;

/* What the drive link reports to the state machine (spec events drive_fault, drive_comms). */
typedef enum { SIMREF_DLE_NONE = 0, SIMREF_DLE_FAULT, SIMREF_DLE_COMMS, SIMREF_DLE_CONFIG } simref_dl_event_t;

typedef struct {
    uint8_t node_id;
    int64_t hb_consumer_us, sdo_timeout_us, transition_limit_us, period_us;
    uint32_t status_every;
    uint16_t expect_1017, expect_2000;  /* boot read R3, R4 (C1) */
} simref_drive_cfg_t;

typedef struct {
    uint16_t index;
    uint8_t sub;
    bool write;
    uint8_t size;
    int32_t value;
} simref_sdo_op_t;

#define SIMREF_DRIVE_MAX_EVENTS 8

typedef struct {
    simref_drive_cfg_t cfg;
    simref_send_fn send;
    void *send_ctx;
    simref_dl_phase_t phase;
    /* heartbeat consumer */
    bool hb_seen;
    int64_t hb_last_us;
    uint8_t hb_state;
    /* SDO client: one outstanding transfer */
    bool sdo_busy;
    simref_sdo_op_t sdo_op;
    int64_t sdo_sent_us;
    /* sequencer */
    int step;
    int64_t step_t0_us;
    int64_t next_cycle_us;
    uint32_t cycle;
    bool nmt_sent;
    /* targets and feedback */
    int32_t target_rpm[2];      /* requested by the controller */
    int32_t sent_rpm[2];        /* last written */
    bool urgent_zero;           /* a non-zero target became zero: write at once */
    int32_t act_rpm10[2];       /* 606Ch, 0.1 r/min */
    bool act_valid[2];
    int64_t act_rx_us[2];
    uint32_t act_seq[2];
    uint16_t statusword;
    uint32_t fault_code;
    uint32_t last_abort;
    uint16_t last_abort_index;
    uint32_t epoch;              /* incremented at every completed boot read (evidence session) */
    bool enable_requested;
    bool boot_ok;
    /* reporting */
    simref_dl_detail_t detail;
    simref_dl_event_t ev[SIMREF_DRIVE_MAX_EVENTS];
    simref_dl_detail_t ev_detail[SIMREF_DRIVE_MAX_EVENTS];
    int n_ev;
    uint32_t enable_sequences, target_writes, nmt_starts;
} simref_drive_t;

void simref_drive_init(simref_drive_t *d, const simref_drive_cfg_t *cfg, simref_send_fn send, void *ctx);
void simref_drive_on_frame(simref_drive_t *d, const simref_frame_t *f, int64_t now_us);
void simref_drive_step(simref_drive_t *d, int64_t now_us);
/* Request a new enable sequence (only taken in IDLE or FAILED). Returns true if started. */
bool simref_drive_request_enable(simref_drive_t *d, int64_t now_us);
/* Disable at once (6040h = 06h) and stop cyclic targets; drive back to IDLE. */
void simref_drive_disable(simref_drive_t *d, int64_t now_us);
void simref_drive_set_targets(simref_drive_t *d, int32_t left_rpm, int32_t right_rpm);
bool simref_drive_enabled(const simref_drive_t *d);
/* Takes the next pending event; returns SIMREF_DLE_NONE when none. */
simref_dl_event_t simref_drive_pop_event(simref_drive_t *d, simref_dl_detail_t *detail);
/* Earliest time at which simref_drive_step has work to do. */
int64_t simref_drive_next_deadline(const simref_drive_t *d);
const char *simref_dl_phase_name(simref_dl_phase_t p);
const char *simref_dl_detail_name(simref_dl_detail_t d);

#endif
