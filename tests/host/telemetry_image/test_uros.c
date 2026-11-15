/* SPDX-License-Identifier: MIT */
/*
 * Host tests of the pure parts of the micro-ROS client
 * (boards/stm32/telemetry_image/src): the connection state machine
 * (tlm_uros_sm.c), the static arena (tlm_uros_alloc.c) and the conversion
 * of the status snapshot to published values (tlm_uros_msg.c), compiled
 * unchanged. No micro-ROS library is needed.
 */
#include <math.h>
#include <string.h>

#include "harness.h"
#include "tlm_uros_alloc.h"
#include "tlm_uros_msg.h"
#include "tlm_uros_sm.h"

/* ---- state machine ---- */
static void waits_for_the_agent_and_retries(void)
{
    tlm_uros_sm_t m;
    tlm_uros_sm_init(&m);
    CHECK(m.state == TLM_UROS_WAIT_AGENT);
    unsigned pings = 0u;
    for (unsigned i = 0u; i < 3u * TLM_UROS_RETRY_PERIODS; i++)
    {
        const tlm_uros_action_t a = tlm_uros_sm_step(&m);
        CHECK(a == TLM_UROS_DO_PING || a == TLM_UROS_DO_NOTHING);
        if (a == TLM_UROS_DO_PING)
        {
            pings++;
            tlm_uros_sm_ping(&m, false);
        }
    }
    CHECK(pings == 3u && m.state == TLM_UROS_WAIT_AGENT && m.sessions == 0u);
}

static tlm_uros_sm_t running(void)
{
    tlm_uros_sm_t m;
    tlm_uros_sm_init(&m);
    CHECK(tlm_uros_sm_step(&m) == TLM_UROS_DO_PING);
    tlm_uros_sm_ping(&m, true);
    CHECK(m.state == TLM_UROS_CREATE && tlm_uros_sm_step(&m) == TLM_UROS_DO_CREATE);
    tlm_uros_sm_created(&m, true);
    CHECK(m.state == TLM_UROS_RUN && m.sessions == 1u);
    return m;
}

static void runs_publishes_and_pings_once_per_second(void)
{
    tlm_uros_sm_t m = running();
    unsigned publish = 0u, ping = 0u;
    for (unsigned i = 0u; i < 3u * TLM_UROS_PING_PERIODS; i++)
    {
        const tlm_uros_action_t a = tlm_uros_sm_step(&m);
        CHECK(a == TLM_UROS_DO_PUBLISH || a == TLM_UROS_DO_PUBLISH_PING);
        publish++;
        if (a == TLM_UROS_DO_PUBLISH_PING)
        {
            ping++;
            tlm_uros_sm_ping(&m, true);
        }
    }
    CHECK(publish == 30u && ping == 3u && m.state == TLM_UROS_RUN);
}

static void destroys_on_loss_and_reconnects(void)
{
    tlm_uros_sm_t m = running();
    unsigned failed = 0u;
    for (unsigned i = 0u; m.state == TLM_UROS_RUN && i < 10u * TLM_UROS_PING_PERIODS; i++)
    {
        if (tlm_uros_sm_step(&m) == TLM_UROS_DO_PUBLISH_PING)
        {
            tlm_uros_sm_ping(&m, false);
            failed++;
        }
        CHECK(failed <= TLM_UROS_PING_LOSS);
    }
    CHECK(failed == TLM_UROS_PING_LOSS && m.state == TLM_UROS_DESTROY && m.losses == 1u);
    CHECK(tlm_uros_sm_step(&m) == TLM_UROS_DO_DESTROY);
    tlm_uros_sm_destroyed(&m);
    CHECK(m.state == TLM_UROS_WAIT_AGENT);
    /* the agent is back */
    CHECK(tlm_uros_sm_step(&m) == TLM_UROS_DO_PING);
    tlm_uros_sm_ping(&m, true);
    (void)tlm_uros_sm_step(&m);
    tlm_uros_sm_created(&m, true);
    CHECK(m.state == TLM_UROS_RUN && m.sessions == 2u && m.losses == 1u);
}

static void one_answered_ping_clears_the_loss_count(void)
{
    tlm_uros_sm_t m = running();
    bool answer = false;
    for (unsigned i = 0u; i < 10u * TLM_UROS_PING_PERIODS; i++)
    {
        if (tlm_uros_sm_step(&m) == TLM_UROS_DO_PUBLISH_PING)
        {
            tlm_uros_sm_ping(&m, answer); /* alternate: never TLM_UROS_PING_LOSS in a row */
            answer = !answer;
        }
    }
    CHECK(m.state == TLM_UROS_RUN && m.losses == 0u);
}

static void creation_failure_tears_down(void)
{
    tlm_uros_sm_t m;
    tlm_uros_sm_init(&m);
    (void)tlm_uros_sm_step(&m);
    tlm_uros_sm_ping(&m, true);
    (void)tlm_uros_sm_step(&m);
    tlm_uros_sm_created(&m, false);
    CHECK(m.state == TLM_UROS_DESTROY && m.create_failures == 1u && m.sessions == 0u);
    /* results outside their state change nothing */
    tlm_uros_sm_created(&m, true);
    tlm_uros_sm_ping(&m, true);
    CHECK(m.state == TLM_UROS_DESTROY);
    CHECK(strcmp(tlm_uros_state_name(m.state), "destroy") == 0);
}

/* ---- arena ---- */
static void arena_is_bounded_and_reset(void)
{
    static uint8_t mem[256];
    tlm_arena_t a;
    tlm_arena_init(&a, mem + 1, sizeof(mem) - 1u); /* misaligned on purpose */
    void *p = tlm_arena_alloc(&a, 10u);
    CHECK(p != NULL && ((uintptr_t)p % 8u) == 0u);
    uint8_t *z = tlm_arena_zalloc(&a, 4u, 4u);
    CHECK(z != NULL && z[0] == 0u && z[15] == 0u);
    memset(p, 0xAB, 10u);
    uint8_t *q = tlm_arena_realloc(&a, p, 40u);
    CHECK(q != NULL && q[0] == 0xABu && q[9] == 0xABu && a.frees == 1u);
    CHECK(tlm_arena_realloc(&a, q, 8u) == q);
    CHECK(tlm_arena_alloc(&a, 1000u) == NULL && a.failures == 1u);
    while (tlm_arena_alloc(&a, 16u) != NULL)
    {
    }
    CHECK(a.failures == 2u && a.used <= a.cap);
    const size_t hw = a.high_water;
    tlm_arena_reset(&a);
    CHECK(a.used == 0u && a.high_water == hw && a.resets == 1u);
    CHECK(tlm_arena_alloc(&a, 16u) != NULL);
    CHECK(tlm_arena_zalloc(&a, (size_t)-1, 4u) == NULL);
    CHECK(tlm_arena_alloc(&a, 0u) == NULL);
}

/* ---- values ---- */
static tlm_status_t snapshot(void)
{
    tlm_status_t s;
    memset(&s, 0, sizeof(s));
    s.boot.id = 3u;
    s.seq = 42u;
    s.uptime_us = 4200123u;
    s.params = "test";
    s.drive.hb_state = "alive";
    s.drive.wheel_ch1 = (tlm_obj_t){.validity = "valid", .raw_known = true, .raw = 1234};
    s.drive.wheel_ch2 = (tlm_obj_t){.validity = "valid", .raw_known = true, .raw = -1234};
    s.drive.tick_missed = 0u;
    s.sensors.tfl[0] = (tlm_tfl_status_t){.name = "usart2", .status = "ok", .valid_known = true,
                                          .valid = true, .distance_known = true, .distance_mm = 1500u};
    s.sensors.tfl[1] = (tlm_tfl_status_t){.name = "usart6", .status = "never"};
    s.aux.lift_hb = "alive";
    s.aux.lift_state = "IDLE";
    s.aux.lift_pos = (tlm_opt_u64_t){true, 5000u};
    s.aux.lift_age_us = (tlm_opt_u64_t){true, 4000u};
    s.aux.bms = "locked";
    return s;
}

static const tlm_uros_meta_t META = {"abcdef123456", "run", 2u, 1u, 0u, 9000u, 1500u};

static void known_values_are_published(void)
{
    tlm_status_t s = snapshot();
    tlm_uros_values_t v;
    tlm_uros_values(&s, &META, &v);
    CHECK(v.wheel[0] == 1234.0f && v.wheel[1] == -1234.0f);
    CHECK(strcmp(v.drive_hb, "alive") == 0);
    CHECK(fabsf(v.range_m[0] - 1.5f) < 1e-6f);
    CHECK(v.stamp_sec == 4u && v.stamp_nanosec == 200123000u);
    CHECK(v.status_len == strlen(v.status) && v.status_len < TLM_UROS_STATUS_MAX);
    CHECK(strcmp(v.status, "{\"boot\":3,\"seq\":42,\"up_us\":4200123,\"commit\":\"abcdef123456\","
                           "\"params\":\"test\",\"uros\":\"run\",\"sessions\":2,\"losses\":1,"
                           "\"tick_miss\":0,\"sens_miss\":0,\"arena\":9000,\"stk\":1500,\"lift\":{\"hb\":\"alive\",\"st\":\"IDLE\","
                           "\"pos\":5000,\"age_us\":4000},\"bms\":\"locked\"}") == 0);
}

static void unknown_values_are_nan_never_zero(void)
{
    tlm_status_t s = snapshot();
    tlm_uros_values_t v;
    /* A stopped drive: timeouts, then stale; a never-read value: not_read. */
    s.drive.wheel_ch1 = (tlm_obj_t){.validity = "timeout"};
    s.drive.wheel_ch2 = (tlm_obj_t){.validity = "stale", .raw_known = false, .raw = 0};
    tlm_uros_values(&s, &META, &v);
    CHECK(isnan(v.wheel[0]) && isnan(v.wheel[1]));
    /* unconfirmed (speed resolution not confirmed) is not valid either */
    s.drive.wheel_ch1 = (tlm_obj_t){.validity = "unconfirmed", .raw_known = true, .raw = 7};
    s.drive.wheel_ch2 = (tlm_obj_t){.validity = "unset"};
    tlm_uros_values(&s, &META, &v);
    CHECK(isnan(v.wheel[0]) && isnan(v.wheel[1]));
    /* TF-Luna never seen, and seen but not valid */
    CHECK(isnan(v.range_m[1]));
    s.sensors.tfl[0].valid = false;
    tlm_uros_values(&s, &META, &v);
    CHECK(isnan(v.range_m[0]));
    /* lift position unknown, battery locked or disabled: never a number */
    s.aux.lift_pos.known = false;
    s.aux.bms = "disabled";
    s.aux.bms_mv = 25600u;
    tlm_uros_values(&s, &META, &v);
    CHECK(strstr(v.status, "\"pos\":\"unknown\"") != NULL && strstr(v.status, "\"bms\":\"disabled\"") != NULL);
    CHECK(strstr(v.status, "25600") == NULL);
}

int main(void)
{
    RUN(waits_for_the_agent_and_retries);
    RUN(runs_publishes_and_pings_once_per_second);
    RUN(destroys_on_loss_and_reconnects);
    RUN(one_answered_ping_clears_the_loss_count);
    RUN(creation_failure_tears_down);
    RUN(arena_is_bounded_and_reset);
    RUN(known_values_are_published);
    RUN(unknown_values_are_nan_never_zero);
    TEST_END();
}
