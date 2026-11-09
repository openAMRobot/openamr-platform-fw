/* SPDX-License-Identifier: MIT */
/*
 * ZLTECH ZLAC8015D V4.x dual-axis drive on CAN1, CANopen (CiA 301 / 402).
 *
 * PROPOSAL for decision D-24 (P-03 item 30 A4): the ZLAC8015D boot
 * configuration and read-back are Evans's ruling. Nothing here is
 * hardware-tested.
 *
 * Object facts used (vendor CANopen routine V1.07/V1.08 labelled V4.0, and its
 * community EDS; confirm against the V4.2 manual shipped with the drive):
 *   6040h controlword (U16), 6041h statusword (U16), 6060h mode (I8, 3 =
 *   profile velocity), 6083h/6084h sub 1 left / sub 2 right acceleration and
 *   deceleration time (U32, ms), 60FFh sub 1/2 target velocity (I32, r/min),
 *   606Ch sub 1/2 actual velocity (I32, 0.1 r/min), 603Fh fault code (U32,
 *   high 16 bits left, low 16 bits right), 1017h producer heartbeat (U16, ms),
 *   2000h communication power-down protection time (ms, 0..32767, default 0),
 *   2010h save to EEPROM. Heartbeat only, no node guarding.
 *
 * Boot sequence (non-blocking):
 *   NMT pre-operational -> for each configuration entry: read; if different,
 *   write and read back; a read-back mismatch or SDO abort latches
 *   CONFIG_FAILED -> save (2010h) only if something was written -> target
 *   velocity 0 on both sides -> NMT start -> controlword 06h, 07h, 0Fh ->
 *   statusword must show Operation Enabled -> OPERATIONAL.
 * Operational cycle per tick: write 60FFh:1/2, read 606Ch:1/2, 603Fh, 6041h.
 * A drive fault latches until oamr_zlac_request_fault_reset() (controlword
 * 80h, then the enable sequence again). Never cleared automatically.
 * Heartbeat consumer: lost after `heartbeat_misses` producer periods; a
 * boot-up message re-runs the whole boot sequence.
 */
#ifndef OAMR_ZLAC8015D_H
#define OAMR_ZLAC8015D_H

#include <stdbool.h>
#include <stdint.h>

#include "oamr/can.h"
#include "oamr/canopen_sdo.h"
#include "oamr/motor_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OAMR_ZLAC_MAX_CONFIG 8u

/* 603Fh bits (per 16-bit half; vendor routine V1.07). */
#define OAMR_ZLAC_FAULT_OVER_VOLTAGE 0x0001u
#define OAMR_ZLAC_FAULT_UNDER_VOLTAGE 0x0002u
#define OAMR_ZLAC_FAULT_OVER_CURRENT 0x0004u
#define OAMR_ZLAC_FAULT_OVERLOAD 0x0008u
#define OAMR_ZLAC_FAULT_ENCODER_TOLERANCE 0x0020u
#define OAMR_ZLAC_FAULT_REFERENCE_VOLTAGE 0x0080u
#define OAMR_ZLAC_FAULT_EEPROM 0x0100u
#define OAMR_ZLAC_FAULT_HALL 0x0200u
#define OAMR_ZLAC_FAULT_MOTOR_OVER_TEMPERATURE 0x0400u
#define OAMR_ZLAC_FAULT_ENCODER 0x0800u
#define OAMR_ZLAC_FAULT_SPEED_SETTING 0x2000u
/* Bits the vendor lists as drive-wide rather than per side. */
#define OAMR_ZLAC_FAULT_DRIVE_WIDE_MASK \
    (OAMR_ZLAC_FAULT_OVER_VOLTAGE | OAMR_ZLAC_FAULT_UNDER_VOLTAGE | OAMR_ZLAC_FAULT_EEPROM)

typedef struct {
    uint32_t raw;          /* 603Fh as read */
    uint16_t left;         /* per-side bits, drive-wide bits removed */
    uint16_t right;
    uint16_t drive_wide;   /* over/under-voltage, EEPROM, from either half */
} oamr_zlac_fault_t;

typedef struct {
    uint16_t index;
    uint8_t subindex;
    uint8_t size;          /* 1, 2 or 4 bytes */
    uint32_t value;
} oamr_zlac_config_entry_t;

typedef struct {
    uint8_t node_id;                     /* 1 (vendor default, 200Ah) */
    uint64_t sdo_timeout_us;             /* TODO(Evans): bench value */
    uint16_t heartbeat_period_ms;        /* 1017h; TODO(Evans): proposed 100 ms (EDS default 500) */
    uint8_t heartbeat_misses;            /* 3 (I8 WP) */
    uint16_t comm_loss_protection_ms;    /* 2000h = 250 ms (work package; P-03 range 200 to 300) */
    uint32_t accel_time_ms;              /* 6083h; TODO(Evans): value that gives the agreed ramp */
    uint32_t decel_time_ms;              /* 6084h; TODO(Evans): value that gives 1.5 m/s^2 */
    uint8_t object_2000_size;            /* TODO(Evans): 2 assumed, not in the EDS */
    uint8_t object_2010_size;            /* TODO(Evans): 2 assumed, not in the EDS */
    uint64_t retry_delay_us;             /* wait before re-running the boot sequence */
    uint8_t max_sdo_failures;            /* consecutive failures before COMM_LOST */
    bool invert_left;                    /* TODO(Evans): wheel direction from the wheels-up bench */
    bool invert_right;                   /* TODO(Evans): wheel direction from the wheels-up bench */
    int32_t max_rpm;                     /* vendor range of 60FFh: 1000 r/min */
} oamr_zlac_config_t;

typedef enum {
    OAMR_ZLAC_INIT = 0,
    OAMR_ZLAC_CONFIGURING,
    OAMR_ZLAC_SAVING,
    OAMR_ZLAC_ZEROING,
    OAMR_ZLAC_STARTING,
    OAMR_ZLAC_ENABLING,
    OAMR_ZLAC_OPERATIONAL,
    OAMR_ZLAC_FAULT,           /* drive reports a fault; latched */
    OAMR_ZLAC_COMM_LOST,       /* heartbeat lost or repeated SDO failures */
    OAMR_ZLAC_CONFIG_FAILED    /* read-back mismatch or abort during boot; latched */
} oamr_zlac_state_t;

typedef struct {
    oamr_zlac_config_t cfg;
    oamr_can_tx_t tx;
    oamr_sdo_client_t sdo;
    oamr_zlac_state_t state;
    uint8_t step;                       /* sub-step in the current state */
    uint8_t phase;                      /* configuration: 0 read, 1 write, 2 verify */
    oamr_zlac_config_entry_t entries[OAMR_ZLAC_MAX_CONFIG];
    uint8_t n_entries;
    bool config_changed;                /* at least one entry written: save needed */
    bool cycle_active;
    /* heartbeat consumer */
    bool hb_seen;
    uint64_t hb_rx_us;
    uint64_t hb_watch_from_us;          /* supervision starts at NMT start */
    bool hb_watch;
    uint8_t nmt_state;
    bool hb_lost_waiting;               /* in COMM_LOST until a heartbeat returns */
    uint64_t state_since_us;
    /* targets and feedback */
    int32_t target_rpm_left;
    int32_t target_rpm_right;
    int32_t fb_left_0p1rpm;
    int32_t fb_right_0p1rpm;
    uint8_t fb_parts;                   /* bit 0 left, bit 1 right, this cycle */
    oamr_wheel_feedback_t feedback;
    uint16_t statusword;
    oamr_zlac_fault_t fault;
    uint16_t last_emcy_code;
    bool fault_reset_requested;
    /* diagnostics */
    uint8_t consecutive_sdo_failures;
    uint32_t sdo_failures_total;
    uint32_t heartbeat_losses;
    uint32_t boot_attempts;
    uint32_t config_writes;             /* entries written during the last boot */
    uint32_t saves;
    uint16_t failed_index;              /* CONFIG_FAILED: entry that failed */
    uint8_t failed_subindex;
    uint32_t failed_abort_code;
} oamr_zlac_t;

void oamr_zlac_default_config(oamr_zlac_config_t *cfg);
void oamr_zlac_init(oamr_zlac_t *z, const oamr_zlac_config_t *cfg, const oamr_can_tx_t *tx,
                    uint64_t now_us);
/* Feed every frame received on CAN1. */
void oamr_zlac_on_frame(oamr_zlac_t *z, const oamr_can_frame_t *f, uint64_t now_us);
/* Call once per control tick, after the motion core has set the targets. */
void oamr_zlac_tick(oamr_zlac_t *z, uint64_t now_us);
void oamr_zlac_set_velocity(oamr_zlac_t *z, float left_rad_s, float right_rad_s);
bool oamr_zlac_ready(const oamr_zlac_t *z, uint64_t now_us);
void oamr_zlac_request_fault_reset(oamr_zlac_t *z);
/* Explicit retry after CONFIG_FAILED. */
void oamr_zlac_request_reinit(oamr_zlac_t *z, uint64_t now_us);
oamr_zlac_fault_t oamr_zlac_decode_fault(uint32_t raw);

/* Motor backend binding for the motion core. The ready check uses `clock_ctx`
 * through the supplied now function. */
typedef struct {
    oamr_zlac_t *zlac;
    uint64_t (*now_us)(void *ctx);
    void *clock_ctx;
} oamr_zlac_backend_ctx_t;
oamr_motor_backend_t oamr_zlac_backend(oamr_zlac_backend_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_ZLAC8015D_H */
