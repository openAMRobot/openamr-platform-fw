/* SPDX-License-Identifier: MIT */
/*
 * Plain data of the console lines and their formatters. No HAL, no RTOS:
 * the formatters are pure functions over these structures.
 *
 * Two periodic lines (README, "Console"):
 *   "st"  every report period, at most TLM_STATUS_LINE_MAX characters: the
 *         values that change from period to period and the counters;
 *   "ax"  in every other period, right after "st", at most TLM_SLOW_LINE_MAX
 *         characters: CTL-017 lift (CAN3) and Daly BMS (CAN2);
 *   "sl"  every TLM_SLOW_EVERY periods, right after that period's "st", at
 *         most TLM_SLOW_LINE_MAX characters: the rarely changing fields, in
 *         two parts that alternate ("cfg" and "diag").
 *
 * Rule for every field: a value that is not known carries known = false and
 * is printed as the JSON string "unknown". It is never printed as 0.
 */
#ifndef TLM_STATUS_H
#define TLM_STATUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool known;
    uint64_t v;
} tlm_opt_u64_t;

typedef struct {
    uint32_t id;          /* count of boots seen by the retained record */
    bool retained;        /* true: record survived the reset; false: cold start */
} tlm_boot_id_t;

/* One object read from the drive by SDO upload. */
typedef struct {
    const char *validity; /* valid, unconfirmed, not_read, timeout, aborted, protocol,
                             tx_failed, bad_size, stale, unset (a parameter it needs has no value) */
    bool raw_known;
    int64_t raw;
    unsigned hex_digits;  /* 0: decimal; else print raw as "0x" with this many digits */
    tlm_opt_u64_t age_us; /* age of the last good sample */
    uint32_t reads;       /* good samples so far (scheduler seq) */
    bool abort_known;
    uint32_t abort_code;
    const char *state;    /* statusword only: CiA 402 state name, or NULL */
} tlm_obj_t;

typedef struct {
    bool can_started;
    const char *hb_state;     /* disabled, waiting, alive, lost */
    tlm_opt_u64_t hb_age_us;
    const char *nmt;          /* bootup, stopped, operational, pre_operational, unknown */
    bool sdo_enabled;
    tlm_obj_t wheel_ch1;      /* 606Ch:01, drive units */
    tlm_obj_t wheel_ch2;      /* 606Ch:02 */
    tlm_obj_t statusword;     /* 6041h */
    tlm_obj_t fault;          /* 603Fh */
    tlm_obj_t resolution;     /* 2026h:05 */
    bool bus_state_known;
    bool bus_off;
    const char *sdo_health;   /* disabled, starting, available, unavailable */
    bool sdo_health_protocol_enabled;
    bool sdo_health_cycle_enabled;
    /* counters */
    uint32_t can_rx, can_tx, can_tx_failed, can_tx_refused;
    uint32_t can_rx_hw_lost;  /* hardware RX FIFO 0 lost a frame (RF0L events) */
    uint32_t can_rx_q_ovf;    /* software receive queue full: frame dropped */
    uint32_t can_rx_deferred; /* interrupt entries that left frames for the next entry */
    uint32_t sdo_ok, sdo_timeouts, sdo_aborts, sdo_protocol, sdo_unsolicited, sdo_wrong_object;
    uint32_t sdo_late;
    uint32_t hb_count, hb_bootups, hb_lost_events, hb_malformed;
    /* can1_tick deadline accounting (tlm_sched.h) */
    uint32_t tick_runs, tick_missed, tick_late_max_ms;
} tlm_drive_status_t;

#define TLM_N_MB7060 4u
#define TLM_N_TFLUNA 2u

typedef struct {
    const char *name;     /* UART the sensor is on: uart4, uart5, uart7, uart8 */
    uint32_t frames;      /* well-formed frames seen (never armed: all unsolicited) */
    uint32_t malformed;
    tlm_opt_u64_t age_us; /* age of the last well-formed frame */
} tlm_mb_status_t;

typedef struct {
    const char *name;     /* usart2, usart6 */
    const char *status;   /* disabled, never, ok, no_return, weak, overexposed, below_min, above_max */
    bool valid_known;
    bool valid;
    bool distance_known;
    uint32_t distance_mm;
    tlm_opt_u64_t age_us;
    uint32_t frames;
    uint32_t checksum_errors;
} tlm_tfl_status_t;

typedef struct {
    tlm_mb_status_t mb[TLM_N_MB7060];
    tlm_tfl_status_t tfl[TLM_N_TFLUNA];
    /* reception state of each port, MB7060 ports first: ok, stopped, failed */
    const char *rx_state[TLM_N_MB7060 + TLM_N_TFLUNA];
    const char *port_name[TLM_N_MB7060 + TLM_N_TFLUNA];
    bool rx_failed[TLM_N_MB7060 + TLM_N_TFLUNA];
    uint32_t uart_errors;
    uint32_t ring_overflows;   /* overflow events, all ports */
    uint32_t ring_lost_bytes;  /* bytes dropped or discarded by overflows, all ports */
    uint32_t rx_start_failures; /* HAL_UART_Receive_IT calls that did not return HAL_OK */
    uint32_t rx_restarts;       /* successful restarts after a stop (first starts excluded) */
} tlm_sensors_status_t;

/* CAN2 (Daly BMS) and CAN3 (CTL-017 lift), receive only. */
typedef struct {
    bool can2_started, can3_started;
    /* lift: values as the node sends them; unknown when not seen */
    const char *lift_hb;        /* disabled, waiting, alive, lost */
    tlm_opt_u64_t lift_hb_age_us;
    const char *lift_nmt;       /* last NMT state of its heartbeat, or unknown */
    const char *lift_state;     /* TPDO1 state name, or unknown */
    tlm_opt_u64_t lift_pos;     /* TPDO1 position, 0.1 mm above the lower end */
    tlm_opt_u64_t lift_age_us;  /* age of the last TPDO1 */
    bool lift_flags_known;
    uint32_t lift_flags;
    bool lift_s2_known;         /* a TPDO2 was seen */
    int32_t lift_vel;           /* 0.1 mm/s, positive up */
    uint32_t lift_ma;
    uint32_t lift_faults;       /* F01..F08 bits 0..7, F09 bit 8 */
    /* BMS: "disabled" (no addresses), "locked" (protocol not confirmed: no
     * value is reported), "usable" (values below) */
    const char *bms;
    bool bms_values_known;
    uint32_t bms_mv;
    int32_t bms_da;             /* 0.1 A, sign as daly_rx.h (direction not named) */
    uint32_t bms_soc_pm;        /* per mille */
    tlm_opt_u64_t bms_age_us;
    /* counters */
    uint32_t c2_rx, c3_rx, c2_lost, c3_lost; /* lost: hardware FIFO loss or queue full */
    uint32_t c2_tx_ref, c3_tx_ref;           /* frames the transmit gate refused */
    uint32_t lift_status, lift_gaps, lift_bad, lift_hb_n, lift_hb_lost, lift_emcy;
    uint32_t bms_dec, bms_foreign, bms_bad, bms_unknown;
} tlm_aux_status_t;

/* Stack high-water marks in words (never used since start). */
#define TLM_N_TASKS 6u
typedef struct {
    const char *name[TLM_N_TASKS];
    uint32_t free_words[TLM_N_TASKS];
} tlm_stack_marks_t;

typedef struct {
    tlm_boot_id_t boot;
    uint32_t seq;
    uint64_t uptime_us;
    const char *params;        /* "default" or "test" */
    tlm_drive_status_t drive;
    tlm_sensors_status_t sensors;
    tlm_aux_status_t aux;
    uint32_t report_overruns;  /* periods the reporter missed */
    uint32_t lines_dropped;    /* lines that did not leave the console in time */
    uint32_t lines_too_long;   /* lines replaced by an "err" record (over their cap) */
    tlm_stack_marks_t stacks;  /* slow line only */
} tlm_status_t;

/* ---- bounded text buffer ---- */
typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool overflow;
} tlm_buf_t;

void tlm_buf_init(tlm_buf_t *b, char *storage, size_t cap);
void tlm_buf_str(tlm_buf_t *b, const char *s);
void tlm_buf_u64(tlm_buf_t *b, uint64_t v);
void tlm_buf_i64(tlm_buf_t *b, int64_t v);
void tlm_buf_hex(tlm_buf_t *b, uint32_t v, unsigned digits);
/* "key": */
void tlm_buf_key(tlm_buf_t *b, const char *key);
/* "key":"text" (text is a constant without characters that need escaping) */
void tlm_buf_kv_str(tlm_buf_t *b, const char *key, const char *text);
void tlm_buf_kv_u64(tlm_buf_t *b, const char *key, uint64_t v);
void tlm_buf_kv_opt(tlm_buf_t *b, const char *key, tlm_opt_u64_t v);
void tlm_buf_kv_bool(tlm_buf_t *b, const char *key, bool v);

/* Names of the features that are disabled in this build or at run time, as a
 * JSON array body ("a","b"). with_reason adds the reason to each entry. */
void tlm_format_disabled(tlm_buf_t *b, bool can_started, bool with_reason);

/* One status line, newline included. Returns false when it did not fit;
 * the buffer content is then not a valid line and must not be sent. */
bool tlm_format_status(const tlm_status_t *s, tlm_buf_t *b);
/* Parts of the slow line, alternating. */
typedef enum {
    TLM_SLOW_CFG = 0, /* params, sdo, health, nmt, res, dis */
    TLM_SLOW_DIAG = 1 /* tick, mb, rx, stk */
} tlm_slow_part_t;
/* Part of the slow line for the status line with this seq (seq is a
 * multiple of TLM_SLOW_EVERY). */
tlm_slow_part_t tlm_slow_part(uint32_t seq);
/* One slow line, newline included; same contract. */
bool tlm_format_slow(const tlm_status_t *s, tlm_slow_part_t part, tlm_buf_t *b);
/* "ax" line (CAN2 and CAN3: lift and BMS): in every period that carries no
 * slow line (seq % TLM_SLOW_EVERY != 0), right after that period's "st", at
 * most TLM_SLOW_LINE_MAX characters. A period therefore carries "st" plus at
 * most one line of the 35 % cap, as before. */
bool tlm_aux_due(uint32_t seq);
bool tlm_format_aux(const tlm_status_t *s, tlm_buf_t *b);

#endif /* TLM_STATUS_H */
