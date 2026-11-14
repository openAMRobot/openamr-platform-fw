/* SPDX-License-Identifier: MIT */
/*
 * Differential-drive odometry maths (pure functions, no state outside the
 * caller's context).
 *
 * Two inputs are supported:
 *   - wheel speeds as raw drive units (for example ZLAC8015D 606Ch) times a
 *     speed resolution in rad/s per unit;
 *   - wheel position counts (for example 6064h) with counts per revolution,
 *     using wrap-safe 32-bit deltas.
 *
 * Every geometric and scaling parameter is supplied by the caller with no
 * default: wheel radius, track width (distance between wheel contact
 * points), counts per revolution, speed resolution, plus the sign of each
 * wheel (+1 or -1) because the drive's left and right meaning is not
 * settled (ZLAC8015D Quick Start contradictions; never assume it).
 * An incomplete or non-finite configuration leaves the context
 * NOT_CONFIGURED and every update is rejected.
 *
 * Rejections: non-finite input, non-positive or too large dt, stale input
 * (sample age at or above max_input_age_us), a sample older than or equal
 * to the previous one (duplicate or out of order), a per-update count jump
 * above max_count_delta. A rejected update never changes the pose and never
 * returns a previous twist as new.
 *
 * Frame: REP 103/105, x forward, y left, yaw counter-clockwise. Right wheel
 * forward motion yields positive linear velocity; v = (vr + vl) / 2,
 * w = (vr - vl) / track.
 */
#ifndef OAMR_DIFF_ODOMETRY_H
#define OAMR_DIFF_ODOMETRY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float wheel_radius_m;          /* TODO(Evans): measured; no default */
    float track_width_m;           /* TODO(Evans): DRIVE-TRACK open; no default */
    float counts_per_rev;          /* TODO(Evans): from the drive EDS; no default */
    float speed_rad_s_per_unit;    /* TODO(Evans): from 606Ch unit and 2026h:05; no default */
    int8_t left_sign;              /* +1 or -1; TODO(Evans) from a wheels-up test */
    int8_t right_sign;             /* +1 or -1; TODO(Evans) from a wheels-up test */
    uint32_t max_input_age_us;     /* sample older than this is stale; no default */
    uint32_t max_count_delta;      /* larger per-update count jump is rejected; no default */
} oamr_odom_config_t;

typedef enum {
    OAMR_ODOM_OK = 0,
    OAMR_ODOM_NOT_CONFIGURED,
    OAMR_ODOM_NON_FINITE,
    OAMR_ODOM_STALE,
    OAMR_ODOM_OUT_OF_ORDER,   /* same or older sample time than the previous one */
    OAMR_ODOM_BAD_DT,
    OAMR_ODOM_JUMP,           /* count delta above max_count_delta */
    OAMR_ODOM_NO_REFERENCE    /* first count sample: stored, no motion computed */
} oamr_odom_result_t;

typedef struct {
    float x_m;
    float y_m;
    float yaw_rad;            /* wrapped to (-pi, pi] */
} oamr_pose2d_t;

typedef struct {
    float v_m_s;
    float w_rad_s;
    float v_left_m_s;
    float v_right_m_s;
} oamr_twist2d_t;

typedef struct {
    oamr_odom_config_t cfg;
    bool configured;
    oamr_pose2d_t pose;
    bool have_counts;
    int32_t last_left_counts;
    int32_t last_right_counts;
    bool have_stamp;
    uint32_t last_sample_us;
    uint32_t rejected;
} oamr_odom_t;

bool oamr_odom_config_valid(const oamr_odom_config_t *cfg);
bool oamr_odom_init(oamr_odom_t *o, const oamr_odom_config_t *cfg);
/* Forget the count reference and sample order (for example after a drive
 * restart). The pose is kept. */
void oamr_odom_reset_reference(oamr_odom_t *o);

/* Wrap-safe signed delta of two 32-bit counters. */
static inline int32_t oamr_count_delta(int32_t now, int32_t prev)
{
    return (int32_t)(uint32_t)((uint32_t)now - (uint32_t)prev);
}

/* Pure: twist from two raw wheel speeds. */
oamr_odom_result_t oamr_odom_twist_from_speeds(const oamr_odom_config_t *cfg, float left_units,
                                               float right_units, oamr_twist2d_t *out);
/* Pure: integrate a body twist over dt (midpoint rule). */
void oamr_odom_integrate(oamr_pose2d_t *pose, float v_m_s, float w_rad_s, float dt_s);

/* Speed path: sample_us is when the speeds were measured, now_us is the
 * caller's current time; dt_s is the integration step. */
oamr_odom_result_t oamr_odom_update_speeds(oamr_odom_t *o, float left_units, float right_units,
                                           uint32_t sample_us, uint32_t now_us, float dt_s,
                                           oamr_twist2d_t *twist);
/* Count path: dt is taken from consecutive sample times. */
oamr_odom_result_t oamr_odom_update_counts(oamr_odom_t *o, int32_t left_counts, int32_t right_counts,
                                           uint32_t sample_us, uint32_t now_us,
                                           oamr_twist2d_t *twist);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_DIFF_ODOMETRY_H */
