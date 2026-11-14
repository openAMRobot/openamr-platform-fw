/* SPDX-License-Identifier: MIT */
#include <string.h>

#include "oamr/daly_rx.h"
#include "test_harness.h"

/* Test addresses only; the real ones are TODO(Evans). */
#define BMS 0x01u
#define HOST 0x40u

static oamr_daly_rx_config_t cfg(void)
{
    oamr_daly_rx_config_t c;
    memset(&c, 0, sizeof(c));
    c.addresses_set = true;
    c.bms_addr = BMS;
    c.host_addr = HOST;
    c.n_cells = 8u;
    c.n_temps = 2u;
    return c;
}

static oamr_daly_rx_frame_t frame(uint8_t data_id, const uint8_t d[8])
{
    oamr_daly_rx_frame_t f;
    f.id = (0x18u << 24) | ((uint32_t)data_id << 16) | (HOST << 8) | BMS;
    f.extended = true;
    f.dlc = 8u;
    memcpy(f.data, d, 8);
    return f;
}

static void config_required(void)
{
    oamr_daly_rx_t d;
    oamr_daly_rx_config_t c = cfg();
    c.addresses_set = false;
    CHECK(!oamr_daly_rx_init(&d, &c));
    const uint8_t z[8] = {0};
    oamr_daly_rx_frame_t f = frame(0x90u, z);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 0u) == OAMR_DALY_RX_NOT_CONFIGURED);
    c = cfg();
    c.n_cells = 0u;
    CHECK(!oamr_daly_rx_init(&d, &c));
    c = cfg();
    c.host_addr = BMS;
    CHECK(!oamr_daly_rx_init(&d, &c));
}

static void unconfirmed_by_default(void)
{
    oamr_daly_rx_t d;
    oamr_daly_rx_config_t c = cfg();
    CHECK(oamr_daly_rx_init(&d, &c));
    CHECK(!oamr_daly_rx_usable(&d));
    const uint8_t p[8] = {0x01, 0x0E, 0x00, 0x00, 0x75, 0x30, 0x02, 0xEE};
    oamr_daly_rx_frame_t f = frame(0x90u, p);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 1u) == OAMR_DALY_RX_DECODED);
    CHECK(!d.data.confirmed);
    c.protocol_confirmed = true;
    CHECK(oamr_daly_rx_init(&d, &c));
    CHECK(oamr_daly_rx_usable(&d));
    CHECK(oamr_daly_rx_on_frame(&d, &f, 1u) == OAMR_DALY_RX_DECODED);
    CHECK(d.data.confirmed);
}

static void soc_frame(void)
{
    oamr_daly_rx_t d;
    oamr_daly_rx_config_t c = cfg();
    CHECK(oamr_daly_rx_init(&d, &c));
    /* 27.0 V, acquired 0, current raw 30000 + 52 -> +5.2 A, SOC 75.0 % */
    const uint8_t p[8] = {0x01, 0x0E, 0x00, 0x00, 0x75, 0x64, 0x02, 0xEE};
    oamr_daly_rx_frame_t f = frame(0x90u, p);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 77u) == OAMR_DALY_RX_DECODED);
    CHECK(d.data.soc_seen && d.data.soc_stamp_us == 77u);
    CHECK_NEAR(d.data.pack_voltage_v, 27.0, 1e-4);
    CHECK_NEAR(d.data.current_a, 5.2, 1e-4);
    CHECK_NEAR(d.data.soc_percent, 75.0, 1e-4);
    /* negative current */
    const uint8_t q[8] = {0x01, 0x0E, 0x00, 0x00, 0x74, 0xCC, 0x03, 0xE8};
    f = frame(0x90u, q);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 78u) == OAMR_DALY_RX_DECODED);
    CHECK_NEAR(d.data.current_a, -10.0, 1e-4);
    CHECK_NEAR(d.data.soc_percent, 100.0, 1e-4);
}

static void minmax_and_mos_and_status(void)
{
    oamr_daly_rx_t d;
    oamr_daly_rx_config_t c = cfg();
    CHECK(oamr_daly_rx_init(&d, &c));
    const uint8_t a[8] = {0x0D, 0x48, 0x03, 0x0C, 0xE4, 0x07, 0, 0}; /* 3400 mV #3, 3300 mV #7 */
    oamr_daly_rx_frame_t f = frame(0x91u, a);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 1u) == OAMR_DALY_RX_DECODED);
    CHECK(d.data.cell_max_mv == 3400u && d.data.cell_max_no == 3u);
    CHECK(d.data.cell_min_mv == 3300u && d.data.cell_min_no == 7u);
    const uint8_t a_bad[8] = {0x0D, 0x48, 0x09, 0x0C, 0xE4, 0x07, 0, 0}; /* cell 9 of 8 */
    f = frame(0x91u, a_bad);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 1u) == OAMR_DALY_RX_MALFORMED);

    const uint8_t t[8] = {65, 1, 0, 2, 0, 0, 0, 0}; /* 25 degC #1, -40 degC #2 */
    f = frame(0x92u, t);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 2u) == OAMR_DALY_RX_DECODED);
    CHECK(d.data.temp_max_c == 25 && d.data.temp_min_c == -40);

    const uint8_t m[8] = {2, 1, 0, 7, 0x00, 0x00, 0x75, 0x30}; /* 30000 mAh */
    f = frame(0x93u, m);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 3u) == OAMR_DALY_RX_DECODED);
    CHECK(d.data.charge_state == 2u && d.data.charge_mos_on && !d.data.discharge_mos_on);
    CHECK(d.data.remaining_mah == 30000u);
    const uint8_t m_bad[8] = {2, 2, 0, 7, 0, 0, 0, 0};
    f = frame(0x93u, m_bad);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 3u) == OAMR_DALY_RX_MALFORMED);

    const uint8_t s[8] = {8, 2, 1, 0, 0x05, 0x01, 0x2C, 0};
    f = frame(0x94u, s);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 4u) == OAMR_DALY_RX_DECODED);
    CHECK(d.data.n_cells_reported == 8u && d.data.charger_present && !d.data.load_present);
    CHECK(d.data.cycles == 300u);
}

static void cell_frames(void)
{
    oamr_daly_rx_t d;
    oamr_daly_rx_config_t c = cfg();
    CHECK(oamr_daly_rx_init(&d, &c));
    /* 8 cells -> frames 1..3, frame 3 carries cells 7, 8 and one unused slot */
    for (uint8_t fr = 1u; fr <= 3u; fr++) {
        uint8_t p[8] = {fr, 0x0D, 0x00, 0x0D, 0x01, 0x0D, 0x02, 0};
        oamr_daly_rx_frame_t f = frame(0x95u, p);
        CHECK(oamr_daly_rx_on_frame(&d, &f, 1u) == OAMR_DALY_RX_DECODED);
    }
    CHECK(d.data.cell_seen_mask == 0xFFu);
    CHECK(d.data.cell_mv[0] == 0x0D00u && d.data.cell_mv[7] == 0x0D01u);
    uint8_t p4[8] = {4, 0, 0, 0, 0, 0, 0, 0};
    oamr_daly_rx_frame_t f = frame(0x95u, p4);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 1u) == OAMR_DALY_RX_MALFORMED);
    uint8_t p0[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    f = frame(0x95u, p0);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 1u) == OAMR_DALY_RX_MALFORMED);

    uint8_t t1[8] = {1, 60, 61, 62, 63, 64, 65, 66};
    f = frame(0x96u, t1);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 1u) == OAMR_DALY_RX_DECODED);
    CHECK(d.data.temp_seen_mask == 0x3u);
    CHECK(d.data.cell_temp_c[0] == 20 && d.data.cell_temp_c[1] == 21);
    uint8_t t2[8] = {2, 0, 0, 0, 0, 0, 0, 0};
    f = frame(0x96u, t2);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 1u) == OAMR_DALY_RX_MALFORMED);
}

static void failure_frame(void)
{
    oamr_daly_rx_t d;
    oamr_daly_rx_config_t c = cfg();
    CHECK(oamr_daly_rx_init(&d, &c));
    const uint8_t none[8] = {0};
    oamr_daly_rx_frame_t f = frame(0x98u, none);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 1u) == OAMR_DALY_RX_DECODED);
    CHECK(d.data.failure_seen && !d.data.any_level1 && !d.data.any_level2 && !d.data.any_hw_failure);
    const uint8_t l1[8] = {0x01, 0, 0, 0, 0, 0, 0, 0};
    f = frame(0x98u, l1);
    oamr_daly_rx_on_frame(&d, &f, 2u);
    CHECK(d.data.any_level1 && !d.data.any_level2);
    const uint8_t l2[8] = {0, 0, 0, 0x80, 0, 0, 0, 0};
    f = frame(0x98u, l2);
    oamr_daly_rx_on_frame(&d, &f, 3u);
    CHECK(!d.data.any_level1 && d.data.any_level2);
    const uint8_t hw[8] = {0, 0, 0, 0, 0, 0, 0x01, 0x05};
    f = frame(0x98u, hw);
    oamr_daly_rx_on_frame(&d, &f, 4u);
    CHECK(d.data.any_hw_failure && d.data.failure_raw[7] == 0x05u);
}

static void filtering(void)
{
    oamr_daly_rx_t d;
    oamr_daly_rx_config_t c = cfg();
    CHECK(oamr_daly_rx_init(&d, &c));
    const uint8_t z[8] = {0};
    oamr_daly_rx_frame_t f = frame(0x90u, z);
    f.extended = false;
    CHECK(oamr_daly_rx_on_frame(&d, &f, 0u) == OAMR_DALY_RX_FOREIGN);
    f = frame(0x90u, z);
    f.id ^= 0x1u; /* other source address */
    CHECK(oamr_daly_rx_on_frame(&d, &f, 0u) == OAMR_DALY_RX_FOREIGN);
    f = frame(0x90u, z);
    f.id = (0x18u << 24) | (0x90u << 16) | (BMS << 8) | HOST; /* request direction */
    CHECK(oamr_daly_rx_on_frame(&d, &f, 0u) == OAMR_DALY_RX_FOREIGN);
    f = frame(0x90u, z);
    f.id = 0x1Cu << 24 | (0x90u << 16) | (HOST << 8) | BMS; /* other priority */
    CHECK(oamr_daly_rx_on_frame(&d, &f, 0u) == OAMR_DALY_RX_FOREIGN);
    f = frame(0x99u, z);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 0u) == OAMR_DALY_RX_UNKNOWN_ID);
    CHECK(d.counters.unknown_id_last == 0x99u);
    f = frame(0x10u, z);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 0u) == OAMR_DALY_RX_UNKNOWN_ID);
    f = frame(0x90u, z);
    f.dlc = 7u;
    CHECK(oamr_daly_rx_on_frame(&d, &f, 0u) == OAMR_DALY_RX_MALFORMED);
    CHECK(d.counters.foreign == 4u && d.counters.unknown_id == 2u && d.counters.malformed == 1u);
    CHECK(!d.data.soc_seen);
    CHECK(oamr_daly_rx_expected_id(&d, 0x90u) == 0x18904001u);
}

static void fuzz(void)
{
    oamr_daly_rx_t d;
    oamr_daly_rx_config_t c = cfg();
    CHECK(oamr_daly_rx_init(&d, &c));
    for (int i = 0; i < 100000; i++) {
        uint8_t p[8];
        for (int k = 0; k < 8; k++) {
            p[k] = (uint8_t)rng_u8();
        }
        oamr_daly_rx_frame_t f = frame((uint8_t)(0x8Fu + rng_u8() % 12u), p);
        f.dlc = (uint8_t)(rng_u8() % 10u == 0u ? rng_u8() % 9u : 8u);
        oamr_daly_rx_on_frame(&d, &f, (uint32_t)i);
    }
    CHECK((d.data.cell_seen_mask & ~0xFFull) == 0u);
    CHECK((d.data.temp_seen_mask & ~0x3u) == 0u);
}

int main(void)
{
    RUN(config_required);
    RUN(unconfirmed_by_default);
    RUN(soc_frame);
    RUN(minmax_and_mos_and_status);
    RUN(cell_frames);
    RUN(failure_frame);
    RUN(filtering);
    RUN(fuzz);
    TEST_MAIN_END();
}
