/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
/*
 * Lift permit producer of the base controller (platform-fw #20, vectors permit_base_*),
 * evidence level, and its encoding for the dedicated permit PDO on the virtual lift bus
 * (CONFLICT C7, SIL layout). Time is int64 microseconds.
 */
#ifndef SIMREF_PERMIT_H
#define SIMREF_PERMIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { SIMREF_POSE_UNKNOWN = 0, SIMREF_POSE_STOWED, SIMREF_POSE_CARRY, SIMREF_POSE_OTHER } simref_pose_t;
typedef enum { SIMREF_SRC_LEFT = 0, SIMREF_SRC_RIGHT, SIMREF_SRC_ARMS, SIMREF_SRC_COUNT } simref_src_t;

typedef enum {
    SIMREF_PR_OK = 0, SIMREF_PR_BOOT, SIMREF_PR_NOT_CONFIGURED, SIMREF_PR_BASE_FEEDBACK_INVALID,
    SIMREF_PR_BASE_MOVING, SIMREF_PR_BASE_STOP_NOT_QUALIFIED, SIMREF_PR_ARM_STATUS_MISSING,
    SIMREF_PR_ARM_STATUS_STALE, SIMREF_PR_ARM_CONTROLLER_FAULT, SIMREF_PR_ARM_MOTION_ACTIVE,
    SIMREF_PR_ARM_POSE_UNSAFE, SIMREF_PR_BASE_FAULT, SIMREF_PR_CTL017_LINK_LOST, SIMREF_PR_COUNT
} simref_permit_reason_t;

typedef enum {
    SIMREF_REJ_NONE = 0, SIMREF_REJ_WRONG_SESSION, SIMREF_REJ_INVALID, SIMREF_REJ_DUPLICATE,
    SIMREF_REJ_OUT_OF_ORDER
} simref_reject_t;

/* Parameter bits for cfg.set: a missing parameter leaves the producer NOT_CONFIGURED. */
enum {
    SIMREF_PP_STOP_V_MAX = 1u << 0, SIMREF_PP_STOP_W_MAX = 1u << 1, SIMREF_PP_STOP_HOLD = 1u << 2,
    SIMREF_PP_WHEEL_STALE = 1u << 3, SIMREF_PP_ARM_STALE = 1u << 4, SIMREF_PP_TRACK = 1u << 5,
    SIMREF_PP_POSES = 1u << 6, SIMREF_PP_ALL = 0x7Fu
};

typedef struct {
    uint32_t set;
    double stop_v_max, stop_w_max, track_m;
    int64_t stop_hold_us, wheel_stale_us, arm_stale_us;
    uint32_t approved_poses;  /* bit per simref_pose_t */
} simref_permit_cfg_t;

typedef struct {
    simref_pose_t pose;
    bool moving;  /* missing reads as moving */
    bool fault;
} simref_arm_t;

typedef struct {
    bool has_session;
    uint32_t session;
    bool has_seq;
    int64_t last_seq;
    bool has_sample;
    int64_t rx_us;
    double v;                     /* wheel sample */
    simref_arm_t left, right;     /* arms sample */
    simref_pose_t bimanual;
} simref_source_t;

#define SIMREF_PERMIT_MAX_REJ 16
typedef struct {
    simref_src_t src;
    simref_reject_t why;
} simref_rejection_t;

typedef struct {
    simref_permit_cfg_t cfg;
    bool configured;
    simref_source_t s[SIMREF_SRC_COUNT];
    bool permit, stop_request, base_fault, ctl_link_ok;
    bool has_stopped_since;
    int64_t stopped_since_us;
    simref_permit_reason_t reason;
    simref_rejection_t rej[SIMREF_PERMIT_MAX_REJ];
    size_t n_rej;
} simref_permit_t;

void simref_permit_init(simref_permit_t *p, const simref_permit_cfg_t *cfg);
void simref_permit_boot(simref_permit_t *p);
void simref_permit_begin_step(simref_permit_t *p);  /* clears the rejection list */
void simref_permit_source_session(simref_permit_t *p, simref_src_t src, uint32_t session);
void simref_permit_wheel(simref_permit_t *p, int64_t now_us, simref_src_t side, uint32_t session,
                         int64_t seq, bool valid, double v);
void simref_permit_arms(simref_permit_t *p, int64_t now_us, uint32_t session, int64_t seq, bool valid,
                        const simref_arm_t *left, const simref_arm_t *right, simref_pose_t bimanual);
void simref_permit_set_base_fault(simref_permit_t *p, bool active);
void simref_permit_set_ctl017_link(simref_permit_t *p, bool ok);
void simref_permit_evaluate(simref_permit_t *p, int64_t now_us);
const char *simref_permit_reason_name(simref_permit_reason_t r);
const char *simref_reject_name(simref_reject_t r);

/* Wire: dedicated permit PDO, SIL layout of platform-sw config/sil.yaml (flag u8, producer
 * boot u16, sequence u16, reason u8, little endian). CONFLICT C7, layout not decided. */
#define SIMREF_PERMIT_COB_ID 0x410u
#define SIMREF_PERMIT_VALUE 1u
typedef struct {
    uint16_t boot_id;
    uint16_t seq;      /* wraps at 65536 */
} simref_permit_tx_t;
/* Encodes the next frame; returns the length (6). */
size_t simref_permit_encode(simref_permit_tx_t *tx, bool permit, uint8_t reason, uint8_t out[8]);

#endif
