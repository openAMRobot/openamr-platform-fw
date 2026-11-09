/* SPDX-License-Identifier: MIT */
/*
 * Core command path: /cmd_vel in, wheel velocity out.
 *
 * Rules (P-03 item 30 A6, I8 WP command policy, E-01 rev 3 motion_allowed):
 * - 200 ms command timeout, evaluated on every control tick, independent of
 *   the host transport or the micro-ROS agent. Non-latching: the next fresh
 *   command resumes (I8 rev 3 section 3.1, Gate A parity).
 * - 500 ms host heartbeat; on loss the stored command is discarded and only a
 *   command received after the link returns is used.
 * - Linear speed and every wheel clamped to 0.5 m/s; when a wheel would exceed
 *   the limit both wheels are scaled by the same factor, so the curvature
 *   (including pure rotation) is kept.
 * - Acceleration and deceleration limited at the wheel (1.5 m/s^2 travel,
 *   1.0 m/s^2 hold-and-move).
 * - motion_allowed is a single input supplied by the safety core. It is
 *   checked on every tick at the only place where wheel targets are written.
 *   While it is false, zero is written. When it becomes true, every stored
 *   command is discarded and only a command received after that time is used.
 * - On timeout, inhibit, host loss or driver not ready the core writes zero at
 *   once; the drive's own deceleration ramp (6084h, configured at boot) then
 *   brings the wheels to rest. Nothing is ever replayed.
 */
#ifndef OAMR_MOTION_H
#define OAMR_MOTION_H

#include <stdbool.h>
#include <stdint.h>

#include "oamr/motor_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t cmd_timeout_us;        /* 200 ms (I8 WP, Gate A parity) */
    uint64_t host_timeout_us;       /* 500 ms host heartbeat (I8 WP) */
    float max_linear_m_s;           /* 0.5 m/s ceiling (P-03 item 29 / 30 A5) */
    float max_wheel_m_s;            /* 0.5 m/s per wheel */
    float accel_limit_m_s2;         /* TODO(Evans): no ruling for acceleration; default = travel decel */
    float decel_limit_m_s2;         /* 1.5 m/s^2 travel (P-03 item 30 A6) */
    float decel_limit_hold_m_s2;    /* 1.0 m/s^2 hold-and-move (P-03 item 30 A6) */
    float wheel_radius_m;           /* TODO(Evans): measured 2.0 hub-motor wheel radius */
    float track_width_m;            /* TODO(Evans): measured 2.0 track width */
    float max_ramp_dt_s;            /* longest tick gap credited to the ramp */
} oamr_motion_config_t;

typedef enum {
    OAMR_APPLIED_OK = 0,            /* fresh command executed (after limits) */
    OAMR_APPLIED_NO_COMMAND,        /* no command accepted since the last gate entry */
    OAMR_APPLIED_TIMEOUT,           /* last command older than cmd_timeout_us */
    OAMR_APPLIED_HOST_LOST,         /* host heartbeat older than host_timeout_us */
    OAMR_APPLIED_DRIVER_NOT_READY,  /* motor backend not ready */
    OAMR_APPLIED_INHIBITED          /* motion_allowed false */
} oamr_applied_state_t;

/* The applied command (I8 rev 3: velocity actually sent after limits, ramp
 * and timeout, with its age and state). */
typedef struct {
    oamr_applied_state_t state;
    float left_m_s;
    float right_m_s;
    float left_rad_s;
    float right_rad_s;
    uint64_t cmd_age_us;            /* UINT64_MAX when no command */
    bool clamped;                   /* the last accepted command was limited */
} oamr_motion_applied_t;

typedef struct {
    uint32_t cmd_received;
    uint32_t cmd_rejected_nonfinite;
    uint32_t cmd_discarded_stale_on_entry;
    uint32_t backend_writes;
} oamr_motion_counters_t;

typedef struct {
    oamr_motion_config_t cfg;
    /* last command */
    bool have_cmd;
    float cmd_v;
    float cmd_w;
    uint64_t cmd_rx_us;
    /* host link */
    bool have_hb;
    uint64_t hb_rx_us;
    bool host_ok_prev;
    /* gate */
    bool ready_prev;                /* motion_allowed && backend ready, last tick */
    uint64_t accept_after_us;       /* commands must be received strictly after this */
    /* ramp state, wheel surface speed */
    float out_left_m_s;
    float out_right_m_s;
    bool ticked;
    uint64_t last_tick_us;
    oamr_motion_applied_t applied;
    oamr_motion_counters_t counters;
} oamr_motion_t;

void oamr_motion_default_config(oamr_motion_config_t *cfg);
void oamr_motion_init(oamr_motion_t *m, const oamr_motion_config_t *cfg);

/* Store a velocity command (base_link frame, m/s and rad/s) received at now_us. */
void oamr_motion_cmd_vel(oamr_motion_t *m, float linear_m_s, float angular_rad_s, uint64_t now_us);

/* Record a host heartbeat received at now_us. */
void oamr_motion_host_heartbeat(oamr_motion_t *m, uint64_t now_us);

/*
 * One control tick. Writes the motor backend exactly once.
 * motion_allowed: from the safety core, no other source.
 * hold_and_move: arms hold a pose, use the hold-and-move deceleration.
 */
const oamr_motion_applied_t *oamr_motion_tick(oamr_motion_t *m, uint64_t now_us,
                                              bool motion_allowed, bool hold_and_move,
                                              const oamr_motor_backend_t *backend);

/* Pure helper: convert (v, w) to wheel surface speeds and apply the linear and
 * per-wheel clamps. Returns true if any limit was applied. */
bool oamr_motion_limit(const oamr_motion_config_t *cfg, float v, float w,
                       float *left_m_s, float *right_m_s);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_MOTION_H */
