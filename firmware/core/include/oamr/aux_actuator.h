/* SPDX-License-Identifier: MIT */
/*
 * Generic auxiliary actuator interface (platform-fw #12): move to a stop,
 * position with validity, limits, fault, hold on power loss.
 *
 * The first device is the lift on CTL-017 (CAN3, node 0x10). The CAN3 relay
 * itself is not part of this change; this header fixes the core-side contract
 * so the interlock can be written and tested against it.
 */
#ifndef OAMR_AUX_ACTUATOR_H
#define OAMR_AUX_ACTUATOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OAMR_AUX_STOP_NONE = 0,
    OAMR_AUX_STOP_L1000 = 1,
    OAMR_AUX_STOP_L1175 = 2,
    OAMR_AUX_STOP_L1350 = 3
} oamr_aux_stop_t;

typedef struct {
    bool link_fresh;          /* status received within the node timeout */
    bool position_valid;
    float position_m;         /* lift_joint, 0 to 0.350 m */
    bool moving;
    oamr_aux_stop_t at_stop;
    oamr_aux_stop_t target_stop;
    bool fault;
    uint32_t fault_bits;
    uint64_t stamp_us;
} oamr_aux_status_t;

typedef struct {
    /* Request a move to one of the calibrated stops. Returns false if refused. */
    bool (*move_to_stop)(void *ctx, oamr_aux_stop_t stop);
    /* Halt now: disable the drive stage; the column holds mechanically. */
    void (*halt)(void *ctx);
    oamr_aux_status_t (*status)(void *ctx);
    void *ctx;
} oamr_aux_actuator_t;

/* Inputs to the lift interlock (platform-fw #20, CAN3 design 5 Nov). */
typedef struct {
    bool base_stopped;        /* under 0.01 m/s and 0.02 rad/s for 0.5 s, fresh */
    bool arms_safe_pose;      /* both arms report stowed or carry, fresh */
    bool aux_status_fresh;
} oamr_aux_interlock_inputs_t;

/* Pure interlock rule: auxiliary motion only with the base stopped, both arms
 * in a declared safe pose and fresh state. Missing or stale = refused. */
static inline bool oamr_aux_motion_permitted(const oamr_aux_interlock_inputs_t *in)
{
    return in->base_stopped && in->arms_safe_pose && in->aux_status_fresh;
}

#ifdef __cplusplus
}
#endif

#endif /* OAMR_AUX_ACTUATOR_H */
