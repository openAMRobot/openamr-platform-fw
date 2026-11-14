/* SPDX-License-Identifier: MIT */
/*
 * Plain data of one status line and its formatter. No HAL, no RTOS: the
 * formatter is a pure function over these structures.
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
    /* counters */
    uint32_t can_rx, can_tx, can_tx_failed, can_tx_refused, can_rx_lost;
    uint32_t sdo_ok, sdo_timeouts, sdo_aborts, sdo_protocol, sdo_unsolicited, sdo_wrong_object;
    uint32_t hb_count, hb_bootups, hb_lost_events, hb_malformed;
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
    uint32_t uart_errors;
    uint32_t ring_overflows;
} tlm_sensors_status_t;

typedef struct {
    tlm_boot_id_t boot;
    uint32_t seq;
    uint64_t uptime_us;
    const char *params;        /* "default" or "test" */
    tlm_drive_status_t drive;
    tlm_sensors_status_t sensors;
    uint32_t report_overruns;  /* periods the reporter missed */
    uint32_t lines_dropped;    /* lines that did not leave the console in time */
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

#endif /* TLM_STATUS_H */
