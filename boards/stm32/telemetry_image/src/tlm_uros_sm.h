/* SPDX-License-Identifier: MIT */
/*
 * Connection state machine of the micro-ROS client (TelemetryUrosSerialSimBench
 * preset). Pure C, no micro-ROS, no RTOS: tlm_uros.c performs the actions and
 * feeds the results back; the host tests drive it directly.
 *
 *   WAIT_AGENT --ping ok--> CREATE --entities created--> RUN
 *       ^  |ping failed: stay, retry after TLM_UROS_RETRY_PERIODS   |
 *       |                          creation failed --> DESTROY <----+ link lost
 *       +---------------------- DESTROY (everything torn down, arena reset)
 *
 * In RUN the link is checked by an agent ping every TLM_UROS_PING_PERIODS
 * report periods; TLM_UROS_PING_LOSS failed pings in a row mean the link is
 * lost. A best-effort publish cannot tell (a serial write succeeds without an
 * agent), so publish results are only counted.
 *
 * The client publishes only: no subscription, no service, no parameter.
 */
#ifndef TLM_UROS_SM_H
#define TLM_UROS_SM_H

#include <stdbool.h>
#include <stdint.h>

/* Source: this image. 100 ms report periods: retry the agent every 0.5 s,
 * ping it once per second in RUN, give it up after 3 s without an answer. */
#define TLM_UROS_RETRY_PERIODS 5u
#define TLM_UROS_PING_PERIODS 10u
#define TLM_UROS_PING_LOSS 3u

typedef enum
{
    TLM_UROS_WAIT_AGENT = 0,
    TLM_UROS_CREATE = 1,
    TLM_UROS_RUN = 2,
    TLM_UROS_DESTROY = 3
} tlm_uros_state_t;

/* What the caller does in this period. */
typedef enum
{
    TLM_UROS_DO_NOTHING = 0,
    TLM_UROS_DO_PING,          /* WAIT_AGENT: ping the agent, report with tlm_uros_sm_ping() */
    TLM_UROS_DO_CREATE,        /* CREATE: create support, node, publishers; tlm_uros_sm_created() */
    TLM_UROS_DO_PUBLISH,       /* RUN: publish this period's values */
    TLM_UROS_DO_PUBLISH_PING,  /* RUN: publish, then ping; tlm_uros_sm_ping() */
    TLM_UROS_DO_DESTROY        /* DESTROY: tear everything down; tlm_uros_sm_destroyed() */
} tlm_uros_action_t;

typedef struct
{
    tlm_uros_state_t state;
    uint32_t period;          /* periods since the state was entered */
    uint32_t ping_fail_row;   /* failed pings in a row in RUN */
    uint32_t sessions;        /* times RUN was entered */
    uint32_t losses;          /* RUN left because the link was lost */
    uint32_t create_failures;
    uint32_t publish_failures;
    uint32_t published;       /* messages handed to rcl in RUN */
} tlm_uros_sm_t;

void tlm_uros_sm_init(tlm_uros_sm_t *m);
/* Once per report period: the action for this period. */
tlm_uros_action_t tlm_uros_sm_step(tlm_uros_sm_t *m);
void tlm_uros_sm_ping(tlm_uros_sm_t *m, bool answered);
void tlm_uros_sm_created(tlm_uros_sm_t *m, bool ok);
void tlm_uros_sm_published(tlm_uros_sm_t *m, uint32_t ok, uint32_t failed);
void tlm_uros_sm_destroyed(tlm_uros_sm_t *m);
const char *tlm_uros_state_name(tlm_uros_state_t s);

#endif /* TLM_UROS_SM_H */
