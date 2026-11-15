/* SPDX-License-Identifier: MIT */
#include <string.h>

#include "oamr/ctl017_rx.h"
#include "test_harness.h"

/*
 * Golden values: openamr-upperbody-fw PR #17 (8dc7236),
 * docs/ctl017_object_dictionary.md, proposal v1 [OD]: node 0x10, TPDO1 0x190,
 * TPDO2 0x290, EMCY 0x090, heartbeat 0x710, 8-byte little-endian PDOs.
 * Payload values are test inputs chosen here.
 */
#define NODE 0x10u

static oamr_ctl017_rx_t lift(void)
{
    oamr_ctl017_rx_t d;
    const oamr_ctl017_rx_config_t c = {.node_id = NODE};
    CHECK(oamr_ctl017_rx_init(&d, &c));
    return d;
}

static oamr_co_frame_t frame(uint32_t id, uint8_t dlc, const uint8_t *p)
{
    oamr_co_frame_t f;
    memset(&f, 0, sizeof(f));
    f.id = id;
    f.dlc = dlc;
    memcpy(f.data, p, dlc);
    return f;
}

static void not_configured_without_node(void)
{
    oamr_ctl017_rx_t d;
    const oamr_ctl017_rx_config_t c = {.node_id = 0u};
    CHECK(!oamr_ctl017_rx_init(&d, &c));
    const uint8_t p[8] = {3, 1, 0x39, 0x30, 0xFF, 0, 0xFF, 0};
    const oamr_co_frame_t f = frame(0x190u, 8u, p);
    CHECK(oamr_ctl017_rx_on_frame(&d, &f, 1u) == OAMR_CTL017_RX_NOT_CONFIGURED);
    CHECK(!oamr_ctl017_position_known(&d));
    CHECK(strcmp(oamr_ctl017_state_name(&d), "unknown") == 0);
}

static void tpdo1_decoded(void)
{
    oamr_ctl017_rx_t d = lift();
    CHECK(!oamr_ctl017_position_known(&d));
    CHECK(strcmp(oamr_ctl017_state_name(&d), "unknown") == 0);
    /* IDLE, position_valid + at_stop, 1234.5 mm (12345 = 0x3039), stop 1, seq 7, result 0, counter 41 */
    const uint8_t p[8] = {3u, 0x03u, 0x39u, 0x30u, 1u, 7u, 0u, 41u};
    const oamr_co_frame_t f = frame(0x190u, 8u, p);
    CHECK(oamr_ctl017_rx_on_frame(&d, &f, 5000u) == OAMR_CTL017_RX_STATUS);
    CHECK(d.data.position_raw == 12345u && d.data.active_stop_id == 1u && d.data.last_seq == 7u);
    CHECK(d.data.cmd_result == 0u && d.data.status_counter == 41u && d.data.status_us == 5000u);
    CHECK(oamr_ctl017_position_known(&d));
    CHECK(strcmp(oamr_ctl017_state_name(&d), "IDLE") == 0);
}

static void position_unknown_is_never_a_number(void)
{
    oamr_ctl017_rx_t d = lift();
    /* position_valid clear: unknown, whatever the bytes say */
    const uint8_t a[8] = {1u, 0x80u, 0x00u, 0x00u, 0xFFu, 0u, 0xFFu, 0u};
    oamr_co_frame_t f = frame(0x190u, 8u, a);
    CHECK(oamr_ctl017_rx_on_frame(&d, &f, 1u) == OAMR_CTL017_RX_STATUS);
    CHECK(!oamr_ctl017_position_known(&d));
    /* valid flag but the 0xFFFF marker: unknown */
    const uint8_t b[8] = {3u, 0x01u, 0xFFu, 0xFFu, 0xFFu, 0u, 0xFFu, 1u};
    f = frame(0x190u, 8u, b);
    (void)oamr_ctl017_rx_on_frame(&d, &f, 2u);
    CHECK(!oamr_ctl017_position_known(&d));
    /* state outside the table */
    const uint8_t c[8] = {9u, 0x01u, 0x10u, 0x00u, 0xFFu, 0u, 0xFFu, 2u};
    f = frame(0x190u, 8u, c);
    (void)oamr_ctl017_rx_on_frame(&d, &f, 3u);
    CHECK(oamr_ctl017_position_known(&d));
    CHECK(strcmp(oamr_ctl017_state_name(&d), "unknown") == 0);
}

static void tpdo2_and_emcy_decoded(void)
{
    oamr_ctl017_rx_t d = lift();
    /* inputs 0x12, target 2, 1500 mA (0x05DC), velocity -250 (0xFF06), faults F02, aux 0x02 */
    const uint8_t p[8] = {0x12u, 2u, 0xDCu, 0x05u, 0x06u, 0xFFu, 0x02u, 0x02u};
    oamr_co_frame_t f = frame(0x290u, 8u, p);
    CHECK(oamr_ctl017_rx_on_frame(&d, &f, 10u) == OAMR_CTL017_RX_STATUS2);
    CHECK(d.data.inputs == 0x12u && d.data.target_stop_id == 2u && d.data.motor_current_ma == 1500u);
    CHECK(d.data.velocity_raw == -250 && d.data.faults_f01_f08 == 0x02u && d.data.aux_flags == 0x02u);
    /* EMCY 0xFF02, error register 0x81, fault 2, mask 0x0002 [OD section 7] */
    const uint8_t e[8] = {0x02u, 0xFFu, 0x81u, 2u, 0x02u, 0x00u, 0u, 0u};
    f = frame(0x090u, 8u, e);
    CHECK(oamr_ctl017_rx_on_frame(&d, &f, 11u) == OAMR_CTL017_RX_EMCY);
    CHECK(d.data.emcy_code == 0xFF02u && d.data.emcy_error_register == 0x81u);
    CHECK(d.data.emcy_fault_no == 2u && d.data.emcy_fault_mask == 0x0002u && d.counters.emcy == 1u);
}

static void filtering_and_malformed(void)
{
    oamr_ctl017_rx_t d = lift();
    const uint8_t p[8] = {3u, 1u, 0u, 1u, 0u, 0u, 0u, 0u};
    oamr_co_frame_t f = frame(0x191u, 8u, p); /* node 0x11 */
    CHECK(oamr_ctl017_rx_on_frame(&d, &f, 1u) == OAMR_CTL017_RX_FOREIGN);
    f = frame(0x190u, 8u, p);
    f.extended = true;
    CHECK(oamr_ctl017_rx_on_frame(&d, &f, 1u) == OAMR_CTL017_RX_FOREIGN);
    f = frame(0x190u, 7u, p);
    CHECK(oamr_ctl017_rx_on_frame(&d, &f, 1u) == OAMR_CTL017_RX_MALFORMED);
    CHECK(!d.data.status_seen);
    f = frame(0x710u, 1u, p); /* heartbeat: left to co_hb_consumer */
    CHECK(oamr_ctl017_rx_on_frame(&d, &f, 1u) == OAMR_CTL017_RX_OTHER);
    f = frame(0x590u, 8u, p); /* SDO response */
    CHECK(oamr_ctl017_rx_on_frame(&d, &f, 1u) == OAMR_CTL017_RX_OTHER);
    CHECK(d.counters.foreign == 2u && d.counters.malformed == 1u && d.counters.other == 2u);
    CHECK(d.counters.status == 0u);
}

static void counter_gaps_counted_only(void)
{
    oamr_ctl017_rx_t d = lift();
    const uint8_t p[8] = {3u, 1u, 0x10u, 0x00u, 0xFFu, 0u, 0xFFu, 254u};
    oamr_co_frame_t f = frame(0x190u, 8u, p);
    (void)oamr_ctl017_rx_on_frame(&d, &f, 1u);
    f.data[7] = 255u;
    (void)oamr_ctl017_rx_on_frame(&d, &f, 2u);
    f.data[7] = 0u; /* wrap 255 -> 0 is not a gap [OD section 3] */
    (void)oamr_ctl017_rx_on_frame(&d, &f, 3u);
    CHECK(d.counters.counter_gaps == 0u);
    f.data[7] = 3u; /* 1 and 2 missing */
    (void)oamr_ctl017_rx_on_frame(&d, &f, 4u);
    CHECK(d.counters.counter_gaps == 1u && d.counters.status == 4u);
    CHECK(oamr_ctl017_position_known(&d)); /* a gap changes no value */
}

int main(void)
{
    RUN(not_configured_without_node);
    RUN(tpdo1_decoded);
    RUN(position_unknown_is_never_a_number);
    RUN(tpdo2_and_emcy_decoded);
    RUN(filtering_and_malformed);
    RUN(counter_gaps_counted_only);
    TEST_MAIN_END();
}
