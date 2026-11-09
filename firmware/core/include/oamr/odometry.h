/* SPDX-License-Identifier: MIT */
/*
 * Differential-drive wheel odometry for /odom/unfiltered.
 *
 * Input: measured wheel speeds from the motor backend (rad/s, already in the
 * robot convention: positive = forward on both sides) with their receive
 * time. Output: planar pose in the odom frame and the body twist.
 * Integration is midpoint (second-order) over the time between two feedback
 * samples. A gap longer than max_gap_us is not integrated (the pose holds and
 * the gap is counted), so a lost drive never produces a jump.
 *
 * Geometry (wheel radius, track width) comes only from the commissioning
 * record; while it is not valid, oamr_odom_update() refuses to integrate.
 */
#ifndef OAMR_ODOMETRY_H
#define OAMR_ODOMETRY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double x_m;
    double y_m;
    double yaw_rad;            /* wrapped to (-pi, pi] */
    float linear_m_s;          /* body twist from the last update */
    float angular_rad_s;
    uint64_t stamp_us;         /* receive time of the last integrated feedback */
    bool valid;                /* at least one feedback sample integrated */
    uint32_t gaps;             /* updates skipped because of a long gap */
} oamr_odom_t;

void oamr_odom_init(oamr_odom_t *o);

/* Integrate one feedback sample. Returns false (and changes nothing except
 * the gap counter and the stamp) when the geometry is not valid, the sample is
 * not newer than the last one, or the gap exceeds max_gap_us. */
bool oamr_odom_update(oamr_odom_t *o, float left_rad_s, float right_rad_s, uint64_t stamp_us,
                      float wheel_radius_m, float track_width_m, uint64_t max_gap_us);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_ODOMETRY_H */
