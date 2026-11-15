/* SPDX-License-Identifier: MIT */
/* Line caps of the console formatter (src/tlm_format.c), built with the
 * default parameters (the full disabled list). Cap golden values: 1152
 * characters per 100 ms at 115200 baud 8N1 (10 bit times per character),
 * 60 % = 691 and 35 % = 403, newline included (tlm_params.h). */
#include <string.h>

#include "harness.h"
#include "tlm_params.h"
#include "tlm_status.h"

static char g_buf[2048];

static void obj_fill(tlm_obj_t *o, bool aborted)
{
    o->validity = aborted ? "aborted" : "unconfirmed";
    o->raw_known = !aborted;
    o->raw = -2147483647 - 1;
    o->hex_digits = 0u;
    o->age_us.known = !aborted;
    o->age_us.v = 99999999999ull; /* 27 h */
    o->reads = 4294967295u;
    o->abort_known = aborted;
    o->abort_code = 0x06020000u;
    o->state = 0;
}

/* A heavy, still plausible status: long-running, every value present, ages
 * of hours, an aborted object, a statusword with the longest state name, the
 * counters of a bad bus. */
static tlm_status_t heavy(uint32_t counter_value)
{
    tlm_status_t s;
    memset(&s, 0, sizeof(s));
    s.boot.id = 4294967295u;
    s.seq = 4294967295u;
    s.uptime_us = 99999999999999ull; /* 3 years */
    s.params = "default";
    tlm_drive_status_t *d = &s.drive;
    d->can_started = true;
    d->hb_state = "waiting";
    d->hb_age_us.known = true;
    d->hb_age_us.v = 99999999999ull;
    d->nmt = "pre_operational";
    d->sdo_enabled = true;
    d->sdo_health = "unavailable";
    obj_fill(&d->wheel_ch1, false);
    obj_fill(&d->wheel_ch2, true);
    obj_fill(&d->statusword, false);
    d->statusword.validity = "valid";
    d->statusword.hex_digits = 4u;
    d->statusword.state = "QUICK_STOP_ACTIVE";
    obj_fill(&d->fault, false);
    d->fault.validity = "valid";
    d->fault.hex_digits = 8u;
    obj_fill(&d->resolution, true);
    d->bus_state_known = true;
    d->can_rx = d->can_tx = d->can_tx_failed = d->can_rx_q_ovf = counter_value;
    d->sdo_ok = d->sdo_timeouts = d->sdo_aborts = d->sdo_protocol = counter_value;
    d->hb_count = d->hb_lost_events = counter_value;
    d->tick_runs = 4294967295u;
    d->tick_late_max_ms = 4294967295u;
    tlm_sensors_status_t *ss = &s.sensors;
    for (unsigned i = 0; i < TLM_N_TFLUNA; i++)
    {
        ss->tfl[i].name = i == 0 ? "usart2" : "usart6";
        ss->tfl[i].status = "overexposed";
        ss->tfl[i].age_us.known = true;
        ss->tfl[i].age_us.v = 99999999999ull;
        ss->tfl[i].frames = 4294967295u;
    }
    static const char *const names[] = {"uart4", "uart5", "uart7", "uart8", "usart2", "usart6"};
    for (unsigned i = 0; i < TLM_N_MB7060 + TLM_N_TFLUNA; i++)
    {
        if (i < TLM_N_MB7060)
        {
            ss->mb[i].name = names[i];
            ss->mb[i].age_us.known = true;
            ss->mb[i].age_us.v = 99999999999ull;
            ss->mb[i].frames = 4294967295u;
        }
        ss->port_name[i] = names[i];
        ss->rx_state[i] = "stopped";
        ss->rx_failed[i] = true;
    }
    ss->ring_overflows = ss->ring_lost_bytes = counter_value;
    static const char *const tasks[TLM_N_TASKS] = {"can1_tick", "can1_rx", "sensors", "reporter",
                                                   "idle"};
    for (unsigned i = 0; i < TLM_N_TASKS; i++)
    {
        s.stacks.name[i] = tasks[i];
        s.stacks.free_words[i] = 65535u;
    }
    return s;
}

static size_t format_len(bool slow, tlm_slow_part_t part, const tlm_status_t *s, size_t cap,
                         bool *ok)
{
    tlm_buf_t b;
    memset(g_buf, 0, sizeof(g_buf));
    tlm_buf_init(&b, g_buf, cap < sizeof(g_buf) ? cap : sizeof(g_buf) - 1u);
    *ok = slow ? tlm_format_slow(s, part, &b) : tlm_format_status(s, &b);
    return b.len;
}

static void caps_follow_the_baud_rate(void)
{
    CHECK(TLM_CONSOLE_CHARS_PER_PERIOD == 1152u);
    CHECK(TLM_STATUS_LINE_MAX == 691u);
    CHECK(TLM_SLOW_LINE_MAX == 403u);
    CHECK(TLM_STATUS_LINE_MAX + TLM_SLOW_LINE_MAX <= TLM_CONSOLE_CHARS_PER_PERIOD * 95u / 100u);
}

static tlm_status_t day_long(uint32_t counter_value)
{
    /* A day-long run: every value present, ages of hours where they can grow,
     * read counts in the millions. */
    tlm_status_t s = heavy(counter_value);
    s.boot.id = 1000u;
    s.seq = 864000u;
    s.uptime_us = 86400000000ull;
    tlm_obj_t *objs[] = {&s.drive.wheel_ch1, &s.drive.wheel_ch2, &s.drive.statusword,
                         &s.drive.fault};
    /* A valid drive sample is never older than the drive age limit (0.3 s in
     * the test values), so its age has at most six digits; the heartbeat and
     * TF-Luna ages grow without bound while the source is silent. */
    for (unsigned i = 0; i < 4u; i++)
    {
        objs[i]->reads = 4320000u;
        objs[i]->age_us.v = 299999u;
        objs[i]->raw = -12345;
    }
    for (unsigned i = 0; i < TLM_N_TFLUNA; i++)
    {
        s.sensors.tfl[i].frames = 8640000u;
        s.sensors.tfl[i].age_us.v = 9999999999ull;
    }
    return s;
}

static void heavy_lines_fit_their_caps(void)
{
    /* ... on a bad bus: thirteen counters of five digits. */
    tlm_status_t s = day_long(99999u);
    bool ok = false;
    const size_t st = format_len(false, TLM_SLOW_CFG, &s, TLM_STATUS_LINE_MAX, &ok);
    CHECK(ok && st <= TLM_STATUS_LINE_MAX && g_buf[st - 1u] == '\n');
    CHECK(strstr(g_buf, "\"cnt_more\":") != NULL); /* some counters did not fit, and it says so */
    printf("  day-long bad-bus status line: %zu characters (cap %u)\n", st,
           (unsigned)TLM_STATUS_LINE_MAX);
    const size_t cfg = format_len(true, TLM_SLOW_CFG, &s, TLM_SLOW_LINE_MAX, &ok);
    CHECK(ok && cfg <= TLM_SLOW_LINE_MAX);
    printf("  slow cfg line: %zu characters (cap %u)\n", cfg, (unsigned)TLM_SLOW_LINE_MAX);
    /* Two ports not receiving (not all six), MB7060 ages of hours. */
    for (unsigned i = 2u; i < TLM_N_MB7060 + TLM_N_TFLUNA; i++)
    {
        s.sensors.rx_state[i] = "ok";
    }
    const size_t diag = format_len(true, TLM_SLOW_DIAG, &s, TLM_SLOW_LINE_MAX, &ok);
    CHECK(ok && diag <= TLM_SLOW_LINE_MAX);
    printf("  slow diag line: %zu characters (cap %u)\n", diag, (unsigned)TLM_SLOW_LINE_MAX);
    /* A quiet line: every counter fits, no cnt_more. */
    tlm_status_t q = day_long(0u);
    q.drive.can_rx = q.drive.can_tx = q.drive.sdo_ok = q.drive.hb_count = 7u;
    q.sensors.ring_lost_bytes = 7u;
    q.seq = 12u;
    q.uptime_us = 1300000u;
    q.drive.hb_age_us.v = 900u;
    q.drive.wheel_ch1.reads = q.drive.wheel_ch2.reads = 3u;
    q.drive.statusword.reads = q.drive.fault.reads = 3u;
    for (unsigned i = 0; i < TLM_N_TFLUNA; i++)
    {
        q.sensors.tfl[i].status = "ok";
        q.sensors.tfl[i].age_us.v = 30u;
        q.sensors.tfl[i].frames = 120u;
        q.sensors.rx_failed[TLM_N_MB7060 + i] = false;
    }
    (void)format_len(false, TLM_SLOW_CFG, &q, TLM_STATUS_LINE_MAX, &ok);
    CHECK(ok && strstr(g_buf, "cnt_more") == NULL && strstr(g_buf, "\"ring_lost\":7") != NULL);
}

static void a_line_over_its_cap_is_refused_not_cut(void)
{
    /* Every value at its maximum width: even without counters the line does
     * not fit, so the formatter reports it and the reporter sends an error
     * record instead of a cut line. */
    tlm_status_t s = heavy(4294967295u);
    bool ok = true;
    (void)format_len(false, TLM_SLOW_CFG, &s, TLM_STATUS_LINE_MAX, &ok);
    CHECK(!ok);
    const size_t full = format_len(false, TLM_SLOW_CFG, &s, sizeof(g_buf), &ok);
    CHECK(ok && full > TLM_STATUS_LINE_MAX);
    /* With all six ports listed as stopped, the diag part is refused too. */
    (void)format_len(true, TLM_SLOW_DIAG, &s, TLM_SLOW_LINE_MAX, &ok);
    CHECK(!ok);
}

static void slow_parts_alternate(void)
{
    CHECK(tlm_slow_part(0u) == TLM_SLOW_CFG);
    CHECK(tlm_slow_part(TLM_SLOW_EVERY) == TLM_SLOW_DIAG);
    CHECK(tlm_slow_part(2u * TLM_SLOW_EVERY) == TLM_SLOW_CFG);
    tlm_status_t s = heavy(1u);
    bool ok = false;
    const size_t n = format_len(true, TLM_SLOW_CFG, &s, TLM_SLOW_LINE_MAX, &ok);
    CHECK(ok && strstr(g_buf, "\"part\":\"cfg\"") != NULL && memchr(g_buf, '\n', n) != NULL);
    CHECK(strstr(g_buf, "\"sdo_health_n\"") != NULL); /* default build: criterion disabled */
}

int main(void)
{
    RUN(caps_follow_the_baud_rate);
    RUN(heavy_lines_fit_their_caps);
    RUN(a_line_over_its_cap_is_refused_not_cut);
    RUN(slow_parts_alternate);
    TEST_END();
}
