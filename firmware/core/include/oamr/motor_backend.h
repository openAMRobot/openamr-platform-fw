/* SPDX-License-Identifier: MIT */
/*
 * Motor backend interface (I8 baseline: "zlac_send_pdo(...) is an adapter
 * behind motor_backend.set_velocity(...)").
 *
 * The motion core calls set_wheel_velocity() exactly once per control tick,
 * from one place only (motion.c, motion_tick). Units are SI at the wheel:
 * wheel angular velocity in rad/s, positive = robot forward on that side.
 */
#ifndef OAMR_MOTOR_BACKEND_H
#define OAMR_MOTOR_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;           /* false when no fresh feedback is available */
    float left_rad_s;
    float right_rad_s;
    uint64_t stamp_us;    /* core clock time the feedback was received */
} oamr_wheel_feedback_t;

typedef struct {
    /* Request wheel velocities. Must not block. */
    void (*set_wheel_velocity)(void *ctx, float left_rad_s, float right_rad_s);
    /* True only when the driver is configured, enabled, heartbeat-fresh and
     * free of faults, so that a non-zero request would actually be executed. */
    bool (*is_ready)(void *ctx);
    /* Latest measured wheel velocity. */
    oamr_wheel_feedback_t (*feedback)(void *ctx);
    void *ctx;
} oamr_motor_backend_t;

#ifdef __cplusplus
}
#endif

#endif /* OAMR_MOTOR_BACKEND_H */
