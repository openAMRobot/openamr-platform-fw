/* SPDX-License-Identifier: MIT */
#include "tlm_uros_sm.h"

static void enter(tlm_uros_sm_t *m, tlm_uros_state_t s)
{
    m->state = s;
    m->period = 0u;
    m->ping_fail_row = 0u;
}

void tlm_uros_sm_init(tlm_uros_sm_t *m)
{
    *m = (tlm_uros_sm_t){0};
    enter(m, TLM_UROS_WAIT_AGENT);
}

tlm_uros_action_t tlm_uros_sm_step(tlm_uros_sm_t *m)
{
    const uint32_t p = m->period++;
    switch (m->state)
    {
    case TLM_UROS_WAIT_AGENT:
        return (p % TLM_UROS_RETRY_PERIODS == 0u) ? TLM_UROS_DO_PING : TLM_UROS_DO_NOTHING;
    case TLM_UROS_CREATE:
        return TLM_UROS_DO_CREATE;
    case TLM_UROS_RUN:
        return (p % TLM_UROS_PING_PERIODS == TLM_UROS_PING_PERIODS - 1u) ? TLM_UROS_DO_PUBLISH_PING
                                                                         : TLM_UROS_DO_PUBLISH;
    case TLM_UROS_DESTROY:
    default:
        return TLM_UROS_DO_DESTROY;
    }
}

void tlm_uros_sm_ping(tlm_uros_sm_t *m, bool answered)
{
    if (m->state == TLM_UROS_WAIT_AGENT)
    {
        if (answered)
        {
            enter(m, TLM_UROS_CREATE);
        }
        return;
    }
    if (m->state != TLM_UROS_RUN)
    {
        return;
    }
    if (answered)
    {
        m->ping_fail_row = 0u;
        return;
    }
    if (++m->ping_fail_row >= TLM_UROS_PING_LOSS)
    {
        m->losses++;
        enter(m, TLM_UROS_DESTROY);
    }
}

void tlm_uros_sm_created(tlm_uros_sm_t *m, bool ok)
{
    if (m->state != TLM_UROS_CREATE)
    {
        return;
    }
    if (ok)
    {
        m->sessions++;
        enter(m, TLM_UROS_RUN);
    }
    else
    {
        m->create_failures++;
        enter(m, TLM_UROS_DESTROY);
    }
}

void tlm_uros_sm_published(tlm_uros_sm_t *m, uint32_t ok, uint32_t failed)
{
    m->published += ok;
    m->publish_failures += failed;
}

void tlm_uros_sm_destroyed(tlm_uros_sm_t *m)
{
    if (m->state == TLM_UROS_DESTROY)
    {
        enter(m, TLM_UROS_WAIT_AGENT);
    }
}

const char *tlm_uros_state_name(tlm_uros_state_t s)
{
    switch (s)
    {
    case TLM_UROS_WAIT_AGENT:
        return "wait_agent";
    case TLM_UROS_CREATE:
        return "create";
    case TLM_UROS_RUN:
        return "run";
    case TLM_UROS_DESTROY:
    default:
        return "destroy";
    }
}
