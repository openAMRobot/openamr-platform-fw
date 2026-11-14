/* SPDX-License-Identifier: MIT */
/* Read-only ZLAC8015D telemetry decode and the configuration read-back
 * comparator. Expected values in these tests are test vectors, not settings. */
#include "oamr/config_readback.h"
#include "oamr/zlac_telemetry.h"
#include "tharness.h"

#define MS 1000ull

static const oamr_tlm_config_t k_cfg = {.max_age_us = 100 * MS}; /* test value */

static oamr_co_sample_t smp(uint32_t v, uint8_t size, uint64_t t, uint32_t seq)
{
    oamr_co_sample_t s;
    memset(&s, 0, sizeof(s));
    s.status = OAMR_CO_SAMPLE_VALID;
    s.value = v;
    s.size = size;
    s.size_indicated = true;
    s.sample_us = t;
    s.seq = seq;
    return s;
}

static oamr_tlm_resolution_t confirmed_resolution(uint64_t now)
{
    oamr_co_sample_t r = smp(1, 1, now, 1);
    oamr_tlm_resolution_t out;
    oamr_tlm_decode_resolution(&k_cfg, &r, 1, true, 0, now, &out);
    return out;
}

static void resolution(void)
{
    oamr_tlm_resolution_t out;
    oamr_co_sample_t r = smp(1, 1, 0, 1);
    oamr_tlm_decode_resolution(&k_cfg, &r, 1, false, 0, 0, &out);
    CHECK_EQ(out.meta.validity, OAMR_TLM_NOT_CONFIGURED);
    CHECK(!out.confirmed);
    oamr_tlm_decode_resolution(&k_cfg, &r, 1, true, 0, 0, &out);
    CHECK_EQ(out.meta.validity, OAMR_TLM_VALID);
    CHECK(out.confirmed);
    r.value = 10;
    oamr_tlm_decode_resolution(&k_cfg, &r, 1, true, 0, 0, &out);
    CHECK_EQ(out.meta.validity, OAMR_TLM_VALID);
    CHECK(!out.confirmed);
    CHECK_EQ(out.raw, 10u);
    r.status = OAMR_CO_SAMPLE_ABORTED;
    oamr_tlm_decode_resolution(&k_cfg, &r, 1, true, 0, 0, &out);
    CHECK_EQ(out.meta.validity, OAMR_TLM_READ_FAILED);
    CHECK(!out.confirmed);
    oamr_tlm_decode_resolution(&k_cfg, NULL, 1, true, 0, 0, &out);
    CHECK_EQ(out.meta.validity, OAMR_TLM_NOT_READ);
    const oamr_tlm_config_t zero = {.max_age_us = 0};
    r = smp(1, 1, 0, 1);
    oamr_tlm_decode_resolution(&zero, &r, 1, true, 0, 0, &out);
    CHECK_EQ(out.meta.validity, OAMR_TLM_NOT_CONFIGURED);
}

static void velocity_both_channels_independent(void)
{
    const uint64_t now = 1000 * MS;
    oamr_tlm_resolution_t res = confirmed_resolution(now);
    oamr_co_sample_t s1 = smp((uint32_t)123, 4, now - 10 * MS, 7);
    oamr_co_sample_t s2 = smp((uint32_t)-456, 4, now - 10 * MS, 9);
    oamr_tlm_velocity_pair_t p;
    oamr_tlm_decode_velocity_pair(&k_cfg, &s1, &s2, &res, 6, 8, now, &p);
    CHECK(p.both_valid);
    CHECK_EQ(p.ch1.raw, 123);
    CHECK_EQ(p.ch2.raw, -456);

    /* channel 2 aborted: channel 1 stays valid, channel 2 reports its own failure */
    s2.status = OAMR_CO_SAMPLE_ABORTED;
    oamr_tlm_decode_velocity_pair(&k_cfg, &s1, &s2, &res, 6, 8, now, &p);
    CHECK(!p.both_valid);
    CHECK_EQ(p.ch1.meta.validity, OAMR_TLM_VALID);
    CHECK_EQ(p.ch1.raw, 123);
    CHECK_EQ(p.ch2.meta.validity, OAMR_TLM_READ_FAILED);
    CHECK_EQ(p.ch2.raw, 0);

    /* the other way round: channel 1 stale, channel 2 valid */
    s2 = smp((uint32_t)-456, 4, now - 10 * MS, 9);
    s1.sample_us = now - 101 * MS;
    oamr_tlm_decode_velocity_pair(&k_cfg, &s1, &s2, &res, 6, 8, now, &p);
    CHECK(!p.both_valid);
    CHECK_EQ(p.ch1.meta.validity, OAMR_TLM_STALE);
    CHECK_EQ(p.ch1.raw, 0);
    CHECK_EQ(p.ch2.meta.validity, OAMR_TLM_VALID);

    /* exactly at max age is still valid */
    s1.sample_us = now - 100 * MS;
    oamr_tlm_decode_velocity_pair(&k_cfg, &s1, &s2, &res, 6, 8, now, &p);
    CHECK_EQ(p.ch1.meta.validity, OAMR_TLM_VALID);

    /* repeat of the same seq on one channel: not both fresh */
    oamr_tlm_decode_velocity_pair(&k_cfg, &s1, &s2, &res, 7, 8, now, &p);
    CHECK(!p.ch1.meta.fresh_seq);
    CHECK(p.ch2.meta.fresh_seq);
    CHECK(!p.both_valid);

    /* sample stamped in the future */
    s1.sample_us = now + 1;
    oamr_tlm_decode_velocity_pair(&k_cfg, &s1, &s2, &res, 6, 8, now, &p);
    CHECK_EQ(p.ch1.meta.validity, OAMR_TLM_STALE);
}

static void velocity_size_and_resolution(void)
{
    const uint64_t now = 0;
    oamr_tlm_resolution_t res = confirmed_resolution(now);
    oamr_co_sample_t s = smp(5, 2, now, 1);
    oamr_tlm_velocity_t v;
    oamr_tlm_decode_velocity(&k_cfg, &s, &res, 0, now, &v);
    CHECK_EQ(v.meta.validity, OAMR_TLM_BAD_SIZE);
    CHECK_EQ(v.raw, 0);
    /* 4 bytes without size indication is accepted for a 32-bit object */
    s = smp(5, 4, now, 1);
    s.size_indicated = false;
    oamr_tlm_decode_velocity(&k_cfg, &s, &res, 0, now, &v);
    CHECK_EQ(v.meta.validity, OAMR_TLM_VALID);
    /* resolution not confirmed: raw kept, validity UNCONFIRMED */
    s = smp((uint32_t)-1, 4, now, 1);
    oamr_tlm_decode_velocity(&k_cfg, &s, NULL, 0, now, &v);
    CHECK_EQ(v.meta.validity, OAMR_TLM_UNCONFIRMED);
    CHECK_EQ(v.raw, -1);
    oamr_tlm_resolution_t wrong;
    oamr_co_sample_t r = smp(10, 1, now, 1);
    oamr_tlm_decode_resolution(&k_cfg, &r, 1, true, 0, now, &wrong);
    oamr_tlm_decode_velocity(&k_cfg, &s, &wrong, 0, now, &v);
    CHECK_EQ(v.meta.validity, OAMR_TLM_UNCONFIRMED);
    /* both channels unconfirmed: both_valid false */
    oamr_tlm_velocity_pair_t p;
    oamr_tlm_decode_velocity_pair(&k_cfg, &s, &s, &wrong, 0, 0, now, &p);
    CHECK(!p.both_valid);
    /* not read */
    oamr_co_sample_t none;
    memset(&none, 0, sizeof(none));
    oamr_tlm_decode_velocity(&k_cfg, &none, &res, 0, now, &v);
    CHECK_EQ(v.meta.validity, OAMR_TLM_NOT_READ);
    CHECK(strcmp(oamr_tlm_validity_name(OAMR_TLM_UNCONFIRMED), "UNCONFIRMED") == 0);
}

static void fault_halves(void)
{
    oamr_tlm_fault_t f;
    oamr_co_sample_t s = smp(0x02000004u, 4, 0, 1);
    oamr_tlm_decode_fault(&k_cfg, &s, 0, 0, &f);
    CHECK_EQ(f.meta.validity, OAMR_TLM_VALID);
    CHECK_EQ(f.half_a, 0x0200);
    CHECK_EQ(f.half_b, 0x0004);
    CHECK(f.any_fault);
    s = smp(0, 4, 0, 2);
    oamr_tlm_decode_fault(&k_cfg, &s, 1, 0, &f);
    CHECK(!f.any_fault);
    CHECK(f.meta.fresh_seq);
    /* asymmetry: only half b */
    s = smp(0x00000800u, 4, 0, 3);
    oamr_tlm_decode_fault(&k_cfg, &s, 2, 0, &f);
    CHECK_EQ(f.half_a, 0);
    CHECK_EQ(f.half_b, 0x0800);
    /* failed read never leaves old halves */
    s.status = OAMR_CO_SAMPLE_TIMEOUT;
    oamr_tlm_decode_fault(&k_cfg, &s, 2, 0, &f);
    CHECK_EQ(f.meta.validity, OAMR_TLM_READ_FAILED);
    CHECK_EQ(f.half_b, 0);
    CHECK(!f.any_fault);
    s = smp(0x1, 2, 0, 4);
    oamr_tlm_decode_fault(&k_cfg, &s, 0, 0, &f);
    CHECK_EQ(f.meta.validity, OAMR_TLM_BAD_SIZE);
}

static void statusword_states(void)
{
    CHECK_EQ(oamr_402_state_from_statusword(0x0000), OAMR_402_NOT_READY_TO_SWITCH_ON);
    CHECK_EQ(oamr_402_state_from_statusword(0x0040), OAMR_402_SWITCH_ON_DISABLED);
    CHECK_EQ(oamr_402_state_from_statusword(0x0021), OAMR_402_READY_TO_SWITCH_ON);
    CHECK_EQ(oamr_402_state_from_statusword(0x0023), OAMR_402_SWITCHED_ON);
    CHECK_EQ(oamr_402_state_from_statusword(0x0027), OAMR_402_OPERATION_ENABLED);
    CHECK_EQ(oamr_402_state_from_statusword(0x0637), OAMR_402_OPERATION_ENABLED);
    CHECK_EQ(oamr_402_state_from_statusword(0x0007), OAMR_402_QUICK_STOP_ACTIVE);
    CHECK_EQ(oamr_402_state_from_statusword(0x000F), OAMR_402_FAULT_REACTION_ACTIVE);
    CHECK_EQ(oamr_402_state_from_statusword(0x0008), OAMR_402_FAULT);
    CHECK_EQ(oamr_402_state_from_statusword(0x0218), OAMR_402_FAULT);
    CHECK_EQ(oamr_402_state_from_statusword(0x0001), OAMR_402_UNKNOWN);
    CHECK(strcmp(oamr_402_state_name(OAMR_402_FAULT), "FAULT") == 0);
    CHECK(strcmp(oamr_402_state_name(OAMR_402_UNKNOWN), "UNKNOWN") == 0);

    oamr_tlm_statusword_t w;
    oamr_co_sample_t s = smp(0x0E37, 2, 0, 1);
    oamr_tlm_decode_statusword(&k_cfg, &s, 0, 0, &w);
    CHECK_EQ(w.meta.validity, OAMR_TLM_VALID);
    CHECK_EQ(w.state, OAMR_402_OPERATION_ENABLED);
    CHECK(w.voltage_enabled);
    CHECK(!w.quick_stop_active);
    CHECK(w.remote);
    CHECK(w.target_reached);
    CHECK(w.internal_limit);
    CHECK(!w.fault);
    CHECK(!w.warning);
    s = smp(0x0088, 2, 0, 2);
    oamr_tlm_decode_statusword(&k_cfg, &s, 0, 0, &w);
    CHECK(w.fault);
    CHECK(w.warning);
    CHECK(w.quick_stop_active);
    CHECK_EQ(w.state, OAMR_402_FAULT);
    /* wrong size and missing size indication for a 16-bit object */
    s = smp(0x0637, 4, 0, 3);
    oamr_tlm_decode_statusword(&k_cfg, &s, 0, 0, &w);
    CHECK_EQ(w.meta.validity, OAMR_TLM_BAD_SIZE);
    CHECK_EQ(w.state, OAMR_402_UNKNOWN);
    s = smp(0x0637, 2, 0, 3);
    s.size_indicated = false;
    oamr_tlm_decode_statusword(&k_cfg, &s, 0, 0, &w);
    CHECK_EQ(w.meta.validity, OAMR_TLM_BAD_SIZE);
}

/* -------- configuration read-back comparator -------- */

static const oamr_co_object_t k_rb_objs[] = {
    {0x1017, 0x00},
    {0x2000, 0x00},
    {0x2026, 0x05},
};

static void make_sched(oamr_co_upl_t *s, th_bus *bus)
{
    oamr_co_upl_config_t c = {
        .node_id = 1, .objects = k_rb_objs, .n_objects = 3, .timeout_us = 50 * MS,
        .max_retries = 0, .max_retries_set = true, .send = th_send, .send_ctx = bus,
    };
    oamr_co_upl_init(s, &c);
}

static void readback_compare(void)
{
    th_bus bus = {0};
    oamr_co_upl_t s;
    make_sched(&s, &bus);
    /* Test vectors only: values are not drive settings. */
    const oamr_rb_expect_t exp[] = {
        {0x1017, 0x00, 2, 0xFFFF, 0x0064},
        {0x2000, 0x00, 2, 0xFFFF, 0x00FA},
        {0x2026, 0x05, 1, 0xFF, 0x01},
        {0x6060, 0x00, 1, 0xFF, 0x03}, /* not in the list */
        {0x1017, 0x00, 2, 0x0000, 0x0064}, /* bad mask */
    };
    oamr_rb_report_t rep;
    /* nothing read yet: all unverified, never a match */
    oamr_rb_compare(exp, 3, &s, 100 * MS, 0, &rep);
    CHECK(rep.configured);
    CHECK_EQ(rep.unverified, 3);
    CHECK(!rep.all_match);
    CHECK_EQ(rep.entries[0].result, OAMR_RB_UNVERIFIED);

    s.samples[0] = smp(0x0064, 2, 10 * MS, 1);
    s.samples[1] = smp(0x0000, 2, 10 * MS, 1); /* drive still at factory 0 */
    s.samples[2] = smp(0x01, 1, 10 * MS, 1);
    oamr_rb_compare(exp, 5, &s, 100 * MS, 20 * MS, &rep);
    CHECK_EQ(rep.entries[0].result, OAMR_RB_MATCH);
    CHECK_EQ(rep.entries[1].result, OAMR_RB_MISMATCH);
    CHECK_EQ(rep.entries[1].read_value, 0u);
    CHECK_EQ(rep.entries[2].result, OAMR_RB_MATCH);
    CHECK_EQ(rep.entries[3].result, OAMR_RB_NOT_IN_LIST);
    CHECK_EQ(rep.entries[4].result, OAMR_RB_BAD_EXPECT);
    CHECK_EQ(rep.mismatches, 1);
    CHECK_EQ(rep.unverified, 2);
    CHECK(!rep.all_match);

    s.samples[1] = smp(0x00FA, 2, 10 * MS, 2);
    oamr_rb_compare(exp, 3, &s, 100 * MS, 20 * MS, &rep);
    CHECK(rep.all_match);

    /* size mismatch */
    s.samples[1] = smp(0x00FA, 4, 10 * MS, 3);
    oamr_rb_compare(exp, 3, &s, 100 * MS, 20 * MS, &rep);
    CHECK_EQ(rep.entries[1].result, OAMR_RB_SIZE_MISMATCH);
    CHECK(!rep.all_match);

    /* stale and failed reads are unverified */
    s.samples[1] = smp(0x00FA, 2, 10 * MS, 4);
    oamr_rb_compare(exp, 3, &s, 100 * MS, 111 * MS, &rep);
    CHECK_EQ(rep.unverified, 3);
    s.samples[2].status = OAMR_CO_SAMPLE_TIMEOUT;
    oamr_rb_compare(exp, 3, &s, 100 * MS, 20 * MS, &rep);
    CHECK_EQ(rep.entries[2].result, OAMR_RB_UNVERIFIED);
    CHECK_EQ(rep.entries[2].read_value, 0u);

    /* masked compare */
    const oamr_rb_expect_t masked[] = {{0x1017, 0x00, 2, 0x00FF, 0xAB64}};
    s.samples[0] = smp(0x1264, 2, 10 * MS, 5);
    oamr_rb_compare(masked, 1, &s, 100 * MS, 20 * MS, &rep);
    CHECK(rep.all_match);

    /* not configured */
    oamr_rb_compare(exp, 3, &s, 0, 20 * MS, &rep);
    CHECK(!rep.configured);
    CHECK(!rep.all_match);
    oamr_rb_compare(exp, 0, &s, 100 * MS, 20 * MS, &rep);
    CHECK(!rep.configured);
    oamr_rb_compare(NULL, 3, &s, 100 * MS, 20 * MS, &rep);
    CHECK(!rep.configured);
    CHECK(strcmp(oamr_rb_result_name(OAMR_RB_MISMATCH), "MISMATCH") == 0);

    /* the comparator never sent anything */
    CHECK_EQ(bus.n, 0);
}

/* End to end: scheduler plus decoders over golden frames. */
static void end_to_end_read_only(void)
{
    static const oamr_co_object_t objs[] = {
        {0x2026, 0x05}, {0x6041, 0x00}, {0x606C, 0x01}, {0x606C, 0x02}, {0x603F, 0x00},
    };
    th_bus bus = {0};
    oamr_co_upl_t s;
    oamr_co_upl_config_t c = {
        .node_id = 1, .objects = objs, .n_objects = 5, .timeout_us = 50 * MS,
        .max_retries = 1, .max_retries_set = true, .send = th_send, .send_ctx = &bus,
    };
    CHECK_EQ(oamr_co_upl_init(&s, &c), OAMR_CO_UPL_IDLE);
    const uint8_t answers[5][8] = {
        {0x4F, 0x26, 0x20, 0x05, 0x01, 0, 0, 0},
        {0x4B, 0x41, 0x60, 0x00, 0x37, 0x06, 0, 0},
        {0x43, 0x6C, 0x60, 0x01, 0x64, 0x00, 0x00, 0x00},
        {0x43, 0x6C, 0x60, 0x02, 0x9C, 0xFF, 0xFF, 0xFF},
        {0x43, 0x3F, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00},
    };
    uint64_t t = 0;
    for (int i = 0; i < 5; i++) {
        oamr_co_upl_tick(&s, t);
        oamr_co_frame_t f = th_frame(0x581, 8, answers[i]);
        CHECK(oamr_co_upl_on_frame(&s, &f, t + 1 * MS));
        t += 2 * MS;
    }
    CHECK_EQ(s.cycles, 1);
    oamr_tlm_resolution_t res;
    oamr_tlm_decode_resolution(&k_cfg, oamr_co_upl_sample(&s, 0), 1, true, 0, t, &res);
    CHECK(res.confirmed);
    oamr_tlm_statusword_t w;
    oamr_tlm_decode_statusword(&k_cfg, oamr_co_upl_sample(&s, 1), 0, t, &w);
    CHECK_EQ(w.state, OAMR_402_OPERATION_ENABLED);
    oamr_tlm_velocity_pair_t p;
    oamr_tlm_decode_velocity_pair(&k_cfg, oamr_co_upl_sample(&s, 2), oamr_co_upl_sample(&s, 3), &res,
                                  0, 0, t, &p);
    CHECK(p.both_valid);
    CHECK_EQ(p.ch1.raw, 100);
    CHECK_EQ(p.ch2.raw, -100);
    oamr_tlm_fault_t fl;
    oamr_tlm_decode_fault(&k_cfg, oamr_co_upl_sample(&s, 4), 0, t, &fl);
    CHECK_EQ(fl.meta.validity, OAMR_TLM_VALID);
    CHECK(!fl.any_fault);
    /* read-only: every frame on the bus was an upload request */
    CHECK_EQ(bus.n, 5);
    for (unsigned i = 0; i < bus.n; i++) {
        CHECK_EQ(bus.frames[i].data[0], 0x40);
        CHECK_EQ(bus.frames[i].id, 0x601u);
    }
    /* After the stale window every decoder refuses the old sample. */
    const uint64_t late = t + 200 * MS;
    oamr_tlm_decode_velocity_pair(&k_cfg, oamr_co_upl_sample(&s, 2), oamr_co_upl_sample(&s, 3), &res,
                                  0, 0, late, &p);
    CHECK(!p.both_valid);
    CHECK_EQ(p.ch1.meta.validity, OAMR_TLM_STALE);
    CHECK_EQ(p.ch2.meta.validity, OAMR_TLM_STALE);
}

int main(void)
{
    static const th_case cases[] = {
        {"resolution", resolution},
        {"velocity_both_channels_independent", velocity_both_channels_independent},
        {"velocity_size_and_resolution", velocity_size_and_resolution},
        {"fault_halves", fault_halves},
        {"statusword_states", statusword_states},
        {"readback_compare", readback_compare},
        {"end_to_end_read_only", end_to_end_read_only},
    };
    return th_run("telemetry", cases, sizeof(cases) / sizeof(cases[0]));
}
