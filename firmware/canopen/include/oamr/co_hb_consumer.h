/* SPDX-License-Identifier: MIT */
/*
 * Heartbeat consumer bookkeeping for one remote node (CiA 301 1016h idea).
 *
 * REPORTS ONLY. It records when a heartbeat was last seen, the last NMT
 * state, boot-ups and malformed frames, and classifies the link as WAITING,
 * ALIVE or LOST against a caller-supplied timeout. It takes no action: no
 * frame is sent, no motion is touched. What the controller does when the
 * classification is LOST is owner code (platform-fw #47, decision D-24).
 *
 * The timeout has no default. D-24 proposes 300 ms for node 1 on CAN1; that
 * is the owner's number to pass in, not a value stored here. A timeout of 0
 * leaves the consumer NOT_CONFIGURED and every query returns NOT_CONFIGURED.
 *
 * Time is a monotonic microsecond counter supplied by the caller.
 */
#ifndef OAMR_CO_HB_CONSUMER_H
#define OAMR_CO_HB_CONSUMER_H

#include <stdbool.h>
#include <stdint.h>

#include "oamr/co_frame.h"
#include "oamr/co_nmt_emcy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OAMR_CO_HB_NOT_CONFIGURED = 0,
    OAMR_CO_HB_WAITING,   /* no heartbeat seen yet and the timeout since start has not elapsed */
    OAMR_CO_HB_ALIVE,     /* a valid heartbeat within the timeout */
    OAMR_CO_HB_LOST       /* none within the timeout, never seen after the timeout, or clock went back */
} oamr_co_hb_class_t;

typedef struct {
    uint8_t node_id;      /* 1..127 */
    uint64_t timeout_us;  /* no default; 0 = not configured. TODO(Evans): D-24 proposes 300 ms */
} oamr_co_hb_config_t;

typedef struct {
    oamr_co_hb_config_t cfg;
    bool configured;
    uint64_t start_us;            /* when supervision bookkeeping started */
    bool seen;                    /* at least one valid heartbeat since start */
    uint64_t last_seen_us;        /* local receipt time of the last valid heartbeat */
    oamr_co_nmt_state_t last_state;
    uint32_t heartbeats;          /* valid heartbeats, boot-ups included */
    uint32_t bootups;             /* boot-up messages (state 0) */
    uint32_t state_changes;       /* last_state changed between two valid heartbeats */
    uint32_t malformed;           /* right COB-ID, wrong length or state */
    uint32_t clock_errors;        /* poll or frame time earlier than last_seen_us */
    uint32_t lost_events;         /* transitions into LOST seen by classify() */
    oamr_co_hb_class_t last_class;
} oamr_co_hb_consumer_t;

/* Returns NOT_CONFIGURED (and leaves the consumer inert) for a zero timeout
 * or an invalid node id; else WAITING. */
oamr_co_hb_class_t oamr_co_hb_init(oamr_co_hb_consumer_t *c, const oamr_co_hb_config_t *cfg,
                                   uint64_t now_us);

/* Feed every received frame. Returns true if the frame was a heartbeat (valid
 * or malformed) of the supervised node. */
bool oamr_co_hb_on_frame(oamr_co_hb_consumer_t *c, const oamr_co_frame_t *f, uint64_t now_us);

/* Classify at now_us. Pure reporting; updates last_class and lost_events. */
oamr_co_hb_class_t oamr_co_hb_classify(oamr_co_hb_consumer_t *c, uint64_t now_us);

const char *oamr_co_hb_class_name(oamr_co_hb_class_t k);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_CO_HB_CONSUMER_H */
