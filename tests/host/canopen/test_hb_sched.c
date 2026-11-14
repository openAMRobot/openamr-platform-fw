/* SPDX-License-Identifier: MIT */
/* Heartbeat consumer bookkeeping and the read-only upload scheduler. */
#include "oamr/co_hb_consumer.h"
#include "oamr/co_upload_sched.h"
#include "tharness.h"

#define MS 1000ull

/* Test-only timeout value; the firmware has no default. */
#define T_HB_TIMEOUT (300 * MS)

static oamr_co_frame_t hb(uint8_t node, uint8_t state)
{
    const uint8_t b[1] = {state};
    return th_frame(0x700u + node, 1, b);
}

static void hb_not_configured(void)
{
    oamr_co_hb_consumer_t c;
    oamr_co_hb_config_t cfg = {.node_id = 1, .timeout_us = 0};
    CHECK_EQ(oamr_co_hb_init(&c, &cfg, 0), OAMR_CO_HB_NOT_CONFIGURED);
    oamr_co_frame_t f = hb(1, 5);
    CHECK(!oamr_co_hb_on_frame(&c, &f, 10));
    CHECK_EQ(oamr_co_hb_classify(&c, 10), OAMR_CO_HB_NOT_CONFIGURED);
    cfg.timeout_us = T_HB_TIMEOUT;
    cfg.node_id = 0;
    CHECK_EQ(oamr_co_hb_init(&c, &cfg, 0), OAMR_CO_HB_NOT_CONFIGURED);
    CHECK_EQ(oamr_co_hb_init(&c, NULL, 0), OAMR_CO_HB_NOT_CONFIGURED);
    CHECK(strcmp(oamr_co_hb_class_name(OAMR_CO_HB_NOT_CONFIGURED), "NOT_CONFIGURED") == 0);
}

static void hb_waiting_alive_lost(void)
{
    oamr_co_hb_consumer_t c;
    const oamr_co_hb_config_t cfg = {.node_id = 1, .timeout_us = T_HB_TIMEOUT};
    CHECK_EQ(oamr_co_hb_init(&c, &cfg, 1000 * MS), OAMR_CO_HB_WAITING);
    CHECK_EQ(oamr_co_hb_classify(&c, 1299 * MS), OAMR_CO_HB_WAITING);
    /* never seen within the timeout: LOST */
    CHECK_EQ(oamr_co_hb_classify(&c, 1300 * MS), OAMR_CO_HB_LOST);
    CHECK_EQ(c.lost_events, 1);
    CHECK_EQ(oamr_co_hb_classify(&c, 1301 * MS), OAMR_CO_HB_LOST);
    CHECK_EQ(c.lost_events, 1); /* counted once per transition */

    oamr_co_frame_t f = hb(1, 0x05);
    CHECK(oamr_co_hb_on_frame(&c, &f, 1400 * MS));
    CHECK_EQ(oamr_co_hb_classify(&c, 1400 * MS), OAMR_CO_HB_ALIVE);
    CHECK_EQ(c.last_state, OAMR_CO_NMT_OPERATIONAL);
    CHECK_EQ(c.last_seen_us, 1400 * MS);
    CHECK_EQ(oamr_co_hb_classify(&c, 1699 * MS), OAMR_CO_HB_ALIVE);
    CHECK_EQ(oamr_co_hb_classify(&c, 1700 * MS), OAMR_CO_HB_LOST);
    CHECK_EQ(c.lost_events, 2);
    /* recovery is reported, nothing else */
    CHECK(oamr_co_hb_on_frame(&c, &f, 1750 * MS));
    CHECK_EQ(oamr_co_hb_classify(&c, 1750 * MS), OAMR_CO_HB_ALIVE);
}

static void hb_bootup_state_change_malformed(void)
{
    oamr_co_hb_consumer_t c;
    const oamr_co_hb_config_t cfg = {.node_id = 1, .timeout_us = T_HB_TIMEOUT};
    oamr_co_hb_init(&c, &cfg, 0);
    oamr_co_frame_t f = hb(1, 0x05);
    oamr_co_hb_on_frame(&c, &f, 10 * MS);
    f = hb(1, 0x00); /* drive rebooted */
    oamr_co_hb_on_frame(&c, &f, 20 * MS);
    CHECK_EQ(c.bootups, 1);
    CHECK_EQ(c.state_changes, 1);
    CHECK_EQ(c.last_state, OAMR_CO_NMT_BOOTUP);
    f = hb(1, 0x7F);
    oamr_co_hb_on_frame(&c, &f, 30 * MS);
    CHECK_EQ(c.state_changes, 2);
    CHECK_EQ(c.heartbeats, 3);

    /* malformed frames never refresh last_seen */
    const uint8_t two[2] = {0x05, 0x00};
    f = th_frame(0x701, 2, two);
    CHECK(oamr_co_hb_on_frame(&c, &f, 200 * MS));
    f = hb(1, 0x85);
    CHECK(oamr_co_hb_on_frame(&c, &f, 250 * MS));
    f = th_frame(0x701, 0, NULL);
    CHECK(oamr_co_hb_on_frame(&c, &f, 260 * MS));
    CHECK_EQ(c.malformed, 3);
    CHECK_EQ(c.last_seen_us, 30 * MS);
    CHECK_EQ(oamr_co_hb_classify(&c, 330 * MS), OAMR_CO_HB_LOST);

    /* other nodes and other functions are ignored */
    f = hb(2, 0x05);
    CHECK(!oamr_co_hb_on_frame(&c, &f, 340 * MS));
    const uint8_t sdo[8] = {0x43, 0, 0, 0, 0, 0, 0, 0};
    f = th_frame(0x581, 8, sdo);
    CHECK(!oamr_co_hb_on_frame(&c, &f, 340 * MS));
    CHECK_EQ(oamr_co_hb_classify(&c, 340 * MS), OAMR_CO_HB_LOST);
}

static void hb_clock_backwards(void)
{
    oamr_co_hb_consumer_t c;
    const oamr_co_hb_config_t cfg = {.node_id = 1, .timeout_us = T_HB_TIMEOUT};
    oamr_co_hb_init(&c, &cfg, 100 * MS);
    oamr_co_frame_t f = hb(1, 0x05);
    oamr_co_hb_on_frame(&c, &f, 50 * MS); /* before start */
    CHECK(!c.seen);
    CHECK_EQ(c.clock_errors, 1);
    oamr_co_hb_on_frame(&c, &f, 200 * MS);
    oamr_co_hb_on_frame(&c, &f, 150 * MS); /* older than last */
    CHECK_EQ(c.last_seen_us, 200 * MS);
    CHECK_EQ(c.clock_errors, 2);
    CHECK_EQ(oamr_co_hb_classify(&c, 100 * MS), OAMR_CO_HB_LOST);
    CHECK_EQ(c.clock_errors, 3);
}

/* ---------------- upload scheduler ---------------- */

static const oamr_co_object_t k_objs[] = {
    {0x6041, 0x00},
    {0x606C, 0x01},
    {0x606C, 0x02},
};

static oamr_co_upl_config_t sched_cfg(th_bus *bus)
{
    oamr_co_upl_config_t c = {
        .node_id = 1,
        .objects = k_objs,
        .n_objects = 3,
        .timeout_us = 50 * MS, /* test value */
        .max_retries = 1,
        .max_retries_set = true,
        .send = th_send,
        .send_ctx = bus,
    };
    return c;
}

static oamr_co_frame_t resp(uint8_t cs, uint16_t idx, uint8_t sub, uint32_t v)
{
    uint8_t b[8] = {cs, (uint8_t)(idx & 0xFF), (uint8_t)(idx >> 8), sub, 0, 0, 0, 0};
    oamr_co_put_le32(&b[4], v);
    return th_frame(0x581, 8, b);
}

/* Every frame the scheduler ever sent must be an initiate-upload request. */
static void assert_only_uploads(const th_bus *bus)
{
    for (unsigned i = 0; i < bus->n && i < 64u; i++) {
        CHECK_EQ(bus->frames[i].id, 0x601u);
        CHECK_EQ(bus->frames[i].dlc, 8);
        CHECK_EQ(bus->frames[i].data[0], 0x40);
        CHECK_EQ(bus->frames[i].data[4] | bus->frames[i].data[5] | bus->frames[i].data[6] |
                     bus->frames[i].data[7],
                 0);
    }
}

static void sched_not_configured(void)
{
    th_bus bus = {0};
    oamr_co_upl_t s;
    oamr_co_upl_config_t c = sched_cfg(&bus);
    c.max_retries_set = false;
    CHECK_EQ(oamr_co_upl_init(&s, &c), OAMR_CO_UPL_NOT_CONFIGURED);
    c = sched_cfg(&bus);
    c.timeout_us = 0;
    CHECK_EQ(oamr_co_upl_init(&s, &c), OAMR_CO_UPL_NOT_CONFIGURED);
    c = sched_cfg(&bus);
    c.n_objects = 0;
    CHECK_EQ(oamr_co_upl_init(&s, &c), OAMR_CO_UPL_NOT_CONFIGURED);
    c = sched_cfg(&bus);
    c.n_objects = OAMR_CO_UPL_MAX_ENTRIES + 1;
    CHECK_EQ(oamr_co_upl_init(&s, &c), OAMR_CO_UPL_NOT_CONFIGURED);
    c = sched_cfg(&bus);
    c.send = NULL;
    CHECK_EQ(oamr_co_upl_init(&s, &c), OAMR_CO_UPL_NOT_CONFIGURED);
    c = sched_cfg(&bus);
    c.node_id = 0;
    CHECK_EQ(oamr_co_upl_init(&s, &c), OAMR_CO_UPL_NOT_CONFIGURED);
    c = sched_cfg(&bus);
    c.objects = NULL;
    CHECK_EQ(oamr_co_upl_init(&s, &c), OAMR_CO_UPL_NOT_CONFIGURED);
    CHECK_EQ(oamr_co_upl_init(&s, NULL), OAMR_CO_UPL_NOT_CONFIGURED);
    oamr_co_upl_tick(&s, 0);
    oamr_co_upl_tick(&s, 1000 * MS);
    CHECK_EQ(bus.n, 0);
    CHECK(oamr_co_upl_sample(&s, 0) == NULL);
    CHECK(oamr_co_upl_counters(&s, 0) == NULL);
    oamr_co_frame_t f = resp(0x4B, 0x6041, 0, 0x37);
    CHECK(!oamr_co_upl_on_frame(&s, &f, 0));
}

static void sched_cycle_golden(void)
{
    th_bus bus = {0};
    oamr_co_upl_t s;
    oamr_co_upl_config_t c = sched_cfg(&bus);
    CHECK_EQ(oamr_co_upl_init(&s, &c), OAMR_CO_UPL_IDLE);

    oamr_co_upl_tick(&s, 0);
    CHECK_EQ(bus.n, 1);
    const uint8_t want0[8] = {0x40, 0x41, 0x60, 0x00, 0, 0, 0, 0};
    CHECK(memcmp(bus.frames[0].data, want0, 8) == 0);
    /* only one outstanding: further ticks send nothing */
    oamr_co_upl_tick(&s, 1 * MS);
    oamr_co_upl_tick(&s, 2 * MS);
    CHECK_EQ(bus.n, 1);

    oamr_co_frame_t f = resp(0x4B, 0x6041, 0, 0x0637);
    CHECK(oamr_co_upl_on_frame(&s, &f, 3 * MS));
    CHECK_EQ(s.state, OAMR_CO_UPL_IDLE);
    oamr_co_upl_tick(&s, 4 * MS);
    CHECK_EQ(bus.n, 2);
    const uint8_t want1[8] = {0x40, 0x6C, 0x60, 0x01, 0, 0, 0, 0};
    CHECK(memcmp(bus.frames[1].data, want1, 8) == 0);
    f = resp(0x43, 0x606C, 1, 100);
    oamr_co_upl_on_frame(&s, &f, 5 * MS);
    oamr_co_upl_tick(&s, 6 * MS);
    const uint8_t want2[8] = {0x40, 0x6C, 0x60, 0x02, 0, 0, 0, 0};
    CHECK(memcmp(bus.frames[2].data, want2, 8) == 0);
    f = resp(0x43, 0x606C, 2, (uint32_t)-100);
    oamr_co_upl_on_frame(&s, &f, 7 * MS);
    CHECK_EQ(s.cycles, 1);
    oamr_co_upl_tick(&s, 8 * MS);
    CHECK(memcmp(bus.frames[3].data, want0, 8) == 0); /* wrapped */

    const oamr_co_sample_t *s0 = oamr_co_upl_sample(&s, 0);
    CHECK_EQ(s0->status, OAMR_CO_SAMPLE_VALID);
    CHECK_EQ(s0->value, 0x0637u);
    CHECK_EQ(s0->size, 2);
    CHECK_EQ(s0->seq, 1);
    CHECK_EQ(s0->sample_us, 3 * MS);
    CHECK_EQ((int32_t)oamr_co_upl_sample(&s, 2)->value, -100);
    CHECK(oamr_co_upl_sample(&s, 3) == NULL);
    CHECK_EQ(oamr_co_upl_counters(&s, 1)->ok, 1);
    assert_only_uploads(&bus);
}

static void sched_timeout_retry_then_fail(void)
{
    th_bus bus = {0};
    oamr_co_upl_t s;
    oamr_co_upl_config_t c = sched_cfg(&bus);
    oamr_co_upl_init(&s, &c);
    /* first a valid sample so we can see it invalidated later */
    oamr_co_upl_tick(&s, 0);
    oamr_co_frame_t f = resp(0x4B, 0x6041, 0, 0x0637);
    oamr_co_upl_on_frame(&s, &f, 1 * MS);
    /* skip entries 1 and 2 quickly */
    oamr_co_upl_tick(&s, 2 * MS);
    f = resp(0x43, 0x606C, 1, 1);
    oamr_co_upl_on_frame(&s, &f, 2 * MS);
    oamr_co_upl_tick(&s, 3 * MS);
    f = resp(0x43, 0x606C, 2, 2);
    oamr_co_upl_on_frame(&s, &f, 3 * MS);

    /* entry 0 again: no answer */
    oamr_co_upl_tick(&s, 10 * MS);
    const unsigned sent = bus.n;
    oamr_co_upl_tick(&s, 59 * MS);
    CHECK_EQ(bus.n, sent); /* not yet */
    oamr_co_upl_tick(&s, 60 * MS); /* timeout -> retry */
    CHECK_EQ(bus.n, sent + 1);
    CHECK_EQ(oamr_co_upl_counters(&s, 0)->timeouts, 1);
    CHECK_EQ(oamr_co_upl_counters(&s, 0)->retries, 1);
    CHECK_EQ(oamr_co_upl_sample(&s, 0)->status, OAMR_CO_SAMPLE_VALID); /* still the old one during retry */
    oamr_co_upl_tick(&s, 110 * MS); /* second timeout -> give up */
    CHECK_EQ(oamr_co_upl_counters(&s, 0)->timeouts, 2);
    CHECK_EQ(oamr_co_upl_counters(&s, 0)->failed, 1);
    const oamr_co_sample_t *s0 = oamr_co_upl_sample(&s, 0);
    CHECK_EQ(s0->status, OAMR_CO_SAMPLE_TIMEOUT);
    CHECK_EQ(s0->value, 0u);
    CHECK_EQ(s0->seq, 1); /* no new seq for a failure */
    CHECK_EQ(s.cur, 1);
    /* a late answer for entry 0 after the give-up: entry 1 is now outstanding
     * after the next tick, so the late frame is a wrong object, not a sample */
    oamr_co_upl_tick(&s, 111 * MS);
    f = resp(0x4B, 0x6041, 0, 0x0637);
    CHECK(oamr_co_upl_on_frame(&s, &f, 112 * MS));
    CHECK_EQ(s.wrong_object, 1);
    CHECK_EQ(oamr_co_upl_sample(&s, 0)->status, OAMR_CO_SAMPLE_TIMEOUT);
    CHECK_EQ(s.state, OAMR_CO_UPL_BUSY);
    assert_only_uploads(&bus);
}

static void sched_zero_retries(void)
{
    th_bus bus = {0};
    oamr_co_upl_t s;
    oamr_co_upl_config_t c = sched_cfg(&bus);
    c.max_retries = 0;
    oamr_co_upl_init(&s, &c);
    oamr_co_upl_tick(&s, 0);
    oamr_co_upl_tick(&s, 50 * MS);
    CHECK_EQ(oamr_co_upl_sample(&s, 0)->status, OAMR_CO_SAMPLE_TIMEOUT);
    CHECK_EQ(bus.n, 1);
    CHECK_EQ(oamr_co_upl_counters(&s, 0)->retries, 0);
}

static void sched_abort_and_protocol(void)
{
    th_bus bus = {0};
    oamr_co_upl_t s;
    oamr_co_upl_config_t c = sched_cfg(&bus);
    oamr_co_upl_init(&s, &c);
    oamr_co_upl_tick(&s, 0);
    oamr_co_frame_t f = resp(0x80, 0x6041, 0, OAMR_CO_SDO_ABORT_NO_OBJECT);
    oamr_co_upl_on_frame(&s, &f, 1 * MS);
    CHECK_EQ(oamr_co_upl_sample(&s, 0)->status, OAMR_CO_SAMPLE_ABORTED);
    CHECK_EQ(oamr_co_upl_sample(&s, 0)->abort_code, OAMR_CO_SDO_ABORT_NO_OBJECT);
    CHECK_EQ(oamr_co_upl_counters(&s, 0)->aborts, 1);
    CHECK_EQ(s.cur, 1);

    /* segmented response to 606Ch:01 */
    oamr_co_upl_tick(&s, 2 * MS);
    f = resp(0x41, 0x606C, 1, 8);
    oamr_co_upl_on_frame(&s, &f, 3 * MS);
    CHECK_EQ(oamr_co_upl_sample(&s, 1)->status, OAMR_CO_SAMPLE_PROTOCOL);

    /* toggled segment frame to 606Ch:02 */
    oamr_co_upl_tick(&s, 4 * MS);
    const uint8_t seg[8] = {0x10, 1, 2, 3, 4, 5, 6, 7};
    f = th_frame(0x581, 8, seg);
    oamr_co_upl_on_frame(&s, &f, 5 * MS);
    CHECK_EQ(oamr_co_upl_sample(&s, 2)->status, OAMR_CO_SAMPLE_PROTOCOL);
    CHECK_EQ(oamr_co_upl_counters(&s, 2)->protocol_errors, 1);

    /* truncated response */
    oamr_co_upl_tick(&s, 6 * MS);
    f = resp(0x4B, 0x6041, 0, 0x37);
    f.dlc = 5;
    oamr_co_upl_on_frame(&s, &f, 7 * MS);
    CHECK_EQ(oamr_co_upl_sample(&s, 0)->status, OAMR_CO_SAMPLE_PROTOCOL);
    assert_only_uploads(&bus);
}

static void sched_duplicate_and_wrong_node(void)
{
    th_bus bus = {0};
    oamr_co_upl_t s;
    oamr_co_upl_config_t c = sched_cfg(&bus);
    oamr_co_upl_init(&s, &c);
    oamr_co_upl_tick(&s, 0);
    /* wrong node: not ours, nothing changes */
    oamr_co_frame_t f = resp(0x4B, 0x6041, 0, 0x37);
    f.id = 0x582;
    CHECK(!oamr_co_upl_on_frame(&s, &f, 1 * MS));
    CHECK_EQ(s.state, OAMR_CO_UPL_BUSY);
    /* wrong index echo: keep waiting */
    f = resp(0x4B, 0x6040, 0, 0x37);
    CHECK(oamr_co_upl_on_frame(&s, &f, 1 * MS));
    CHECK_EQ(s.state, OAMR_CO_UPL_BUSY);
    CHECK_EQ(s.wrong_object, 1);
    /* the real answer */
    f = resp(0x4B, 0x6041, 0, 0x37);
    oamr_co_upl_on_frame(&s, &f, 2 * MS);
    CHECK_EQ(oamr_co_upl_sample(&s, 0)->seq, 1);
    /* duplicate of the same answer before the next tick: unsolicited, no new seq */
    oamr_co_upl_on_frame(&s, &f, 3 * MS);
    CHECK_EQ(s.unsolicited, 1);
    CHECK_EQ(oamr_co_upl_sample(&s, 0)->seq, 1);
    CHECK_EQ(oamr_co_upl_sample(&s, 0)->sample_us, 2 * MS);
    /* duplicate after the next request went out: wrong object for entry 1 */
    oamr_co_upl_tick(&s, 4 * MS);
    oamr_co_upl_on_frame(&s, &f, 5 * MS);
    CHECK_EQ(s.wrong_object, 2);
    CHECK_EQ(oamr_co_upl_sample(&s, 1)->status, OAMR_CO_SAMPLE_NONE);
}

static void sched_tx_failure(void)
{
    th_bus bus = {0};
    oamr_co_upl_t s;
    oamr_co_upl_config_t c = sched_cfg(&bus);
    oamr_co_upl_init(&s, &c);
    bus.fail_next = 2;
    oamr_co_upl_tick(&s, 0); /* fail, retry pending */
    CHECK_EQ(s.state, OAMR_CO_UPL_IDLE);
    CHECK_EQ(oamr_co_upl_counters(&s, 0)->tx_failures, 1);
    oamr_co_upl_tick(&s, 1 * MS); /* fail again, give up */
    CHECK_EQ(oamr_co_upl_sample(&s, 0)->status, OAMR_CO_SAMPLE_TX_FAILED);
    CHECK_EQ(s.cur, 1);
    oamr_co_upl_tick(&s, 2 * MS);
    CHECK_EQ(bus.n, 1);
    CHECK_EQ(s.state, OAMR_CO_UPL_BUSY);
}

static void sched_clock_backwards(void)
{
    th_bus bus = {0};
    oamr_co_upl_t s;
    oamr_co_upl_config_t c = sched_cfg(&bus);
    c.max_retries = 0;
    oamr_co_upl_init(&s, &c);
    oamr_co_upl_tick(&s, 100 * MS);
    oamr_co_upl_tick(&s, 10 * MS); /* clock went back: treat as timeout */
    CHECK_EQ(oamr_co_upl_sample(&s, 0)->status, OAMR_CO_SAMPLE_TIMEOUT);
}

int main(void)
{
    static const th_case cases[] = {
        {"hb_not_configured", hb_not_configured},
        {"hb_waiting_alive_lost", hb_waiting_alive_lost},
        {"hb_bootup_state_change_malformed", hb_bootup_state_change_malformed},
        {"hb_clock_backwards", hb_clock_backwards},
        {"sched_not_configured", sched_not_configured},
        {"sched_cycle_golden", sched_cycle_golden},
        {"sched_timeout_retry_then_fail", sched_timeout_retry_then_fail},
        {"sched_zero_retries", sched_zero_retries},
        {"sched_abort_and_protocol", sched_abort_and_protocol},
        {"sched_duplicate_and_wrong_node", sched_duplicate_and_wrong_node},
        {"sched_tx_failure", sched_tx_failure},
        {"sched_clock_backwards", sched_clock_backwards},
    };
    return th_run("hb_sched", cases, sizeof(cases) / sizeof(cases[0]));
}
