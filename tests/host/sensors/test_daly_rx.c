/* SPDX-License-Identifier: MIT */
#include <string.h>

#include "oamr/daly_rx.h"
#include "test_harness.h"

/*
 * Golden values in this file and their sources.
 *
 * No CAN capture from the delivered BMS exists. The decoder stays locked
 * (protocol_confirmed = false, oamr_daly_rx_usable() false) and every
 * value below tests the decoder against the documents it cites, not
 * against the unit.
 *
 * [V10]  Daly CAN Communications Protocol V1.0 (2019), identifier layout
 *        0x18 << 24 | DataID << 16 | destination << 8 | source, BMS 0x01,
 *        upper computer 0x40. Section not recorded in daly_rx.h. Source not
 *        verified: no copy of the document was available to this change.
 * [V12]  Daly UART/RS485 protocol V1.2, payloads of Data IDs 0x90 to 0x98
 *        (current offset 30000 at 0.1 A, temperature offset 40, frame
 *        numbering of 0x95 and 0x96 from 1). Section not recorded in
 *        daly_rx.h. Source not verified: no copy of the document was
 *        available to this change.
 * [CALC] hand arithmetic from a [V12] layout with test inputs chosen here.
 * [CFG]  a test configuration value (n_cells, n_temps), not a property of
 *        the delivered unit.
 */

/* Test addresses only; the real ones are TODO(Evans). */
#define BMS 0x01u  /* [V10] documented BMS address; source not verified */
#define HOST 0x40u /* [V10] documented upper computer address; source not verified */

static oamr_daly_rx_config_t cfg(void)
{
    oamr_daly_rx_config_t c;
    memset(&c, 0, sizeof(c));
    c.addresses_set = true;
    c.bms_addr = BMS;
    c.host_addr = HOST;
    c.n_cells = 8u; /* [CFG] */
    c.n_temps = 2u; /* [CFG] */
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
    /* [V12] 0x90 layout, [CALC] values: 0x010E = 270 -> 27.0 V, acquired 0,
     * current raw 0x7564 = 30052 -> +5.2 A, SOC 0x02EE = 750 -> 75.0 % */
    const uint8_t p[8] = {0x01, 0x0E, 0x00, 0x00, 0x75, 0x64, 0x02, 0xEE};
    oamr_daly_rx_frame_t f = frame(0x90u, p);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 77u) == OAMR_DALY_RX_DECODED);
    CHECK(d.data.soc_seen && d.data.soc_stamp_us == 77u);
    CHECK_NEAR(d.data.pack_voltage_v, 27.0, 1e-4);
    CHECK_NEAR(d.data.current_a, 5.2, 1e-4);
    CHECK_NEAR(d.data.soc_percent, 75.0, 1e-4);
    /* [V12] 0x90, [CALC]: current raw 0x74CC = 29900 -> -10.0 A, SOC 0x03E8 -> 100.0 % */
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
    /* [V12] 0x91 layout, [CALC]: 0x0D48 = 3400 mV cell 3, 0x0CE4 = 3300 mV cell 7 */
    const uint8_t a[8] = {0x0D, 0x48, 0x03, 0x0C, 0xE4, 0x07, 0, 0};
    oamr_daly_rx_frame_t f = frame(0x91u, a);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 1u) == OAMR_DALY_RX_DECODED);
    CHECK(d.data.cell_max_mv == 3400u && d.data.cell_max_no == 3u);
    CHECK(d.data.cell_min_mv == 3300u && d.data.cell_min_no == 7u);
    const uint8_t a_bad[8] = {0x0D, 0x48, 0x09, 0x0C, 0xE4, 0x07, 0, 0}; /* cell 9 of [CFG] 8 */
    f = frame(0x91u, a_bad);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 1u) == OAMR_DALY_RX_MALFORMED);

    /* [V12] 0x92 layout and offset 40, [CALC]: 65 -> 25 degC sensor 1, 0 -> -40 degC sensor 2 */
    const uint8_t t[8] = {65, 1, 0, 2, 0, 0, 0, 0};
    f = frame(0x92u, t);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 2u) == OAMR_DALY_RX_DECODED);
    CHECK(d.data.temp_max_c == 25 && d.data.temp_min_c == -40);

    /* [V12] 0x93 layout, [CALC]: state 2, charge MOS on, discharge off,
     * life 7, 0x00007530 = 30000 mAh */
    const uint8_t m[8] = {2, 1, 0, 7, 0x00, 0x00, 0x75, 0x30};
    f = frame(0x93u, m);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 3u) == OAMR_DALY_RX_DECODED);
    CHECK(d.data.charge_state == 2u && d.data.charge_mos_on && !d.data.discharge_mos_on);
    CHECK(d.data.remaining_mah == 30000u);
    const uint8_t m_bad[8] = {2, 2, 0, 7, 0, 0, 0, 0};
    f = frame(0x93u, m_bad);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 3u) == OAMR_DALY_RX_MALFORMED);

    /* [V12] 0x94 layout, [CALC]: 8 cells, 2 sensors, charger, no load, DIO 0x05,
     * 0x012C = 300 cycles */
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
    /* [V12] 0x95: frame number from 1, three cells per frame. [CALC] with
     * [CFG] 8 cells -> frames 1..3, frame 3 carries cells 7, 8 and one unused slot */
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

    /* [V12] 0x96: frame number from 1, seven temperatures, offset 40 [CALC] */
    uint8_t t1[8] = {1, 60, 61, 62, 63, 64, 65, 66};
    f = frame(0x96u, t1);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 1u) == OAMR_DALY_RX_DECODED);
    CHECK(d.data.temp_seen_mask == 0x3u);
    CHECK(d.data.cell_temp_c[0] == 20 && d.data.cell_temp_c[1] == 21);
    uint8_t t2[8] = {2, 0, 0, 0, 0, 0, 0, 0};
    f = frame(0x96u, t2);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 1u) == OAMR_DALY_RX_MALFORMED);
}

/* [V12] 0x98: bytes 0..3 alarm bits (even bit level 1, odd bit level 2),
 * bytes 4..6 hardware failures; masks 0x55 and 0xAA in daly_rx.h. */
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
    CHECK(oamr_daly_rx_expected_id(&d, 0x90u) == 0x18904001u); /* [V10] layout, [CALC] */
}

static void negative_current(void)
{
    /* [V12] current = (raw - 30000) * 0.1 A; [CALC] for each raw value. The
     * decoder reports the signed value and names no direction (the public
     * sources disagree on the sign; TODO(Evans) in daly_rx.h). */
    oamr_daly_rx_t d;
    oamr_daly_rx_config_t c = cfg();
    CHECK(oamr_daly_rx_init(&d, &c));
    const struct
    {
        uint8_t hi, lo;
        double amps;
    } v[] = {
        {0x75, 0x30, 0.0},     /* 30000 */
        {0x75, 0x2F, -0.1},    /* 29999: smallest negative step */
        {0x74, 0xCC, -10.0},   /* 29900 */
        {0x3A, 0x98, -1500.0}, /* 15000 */
        {0x00, 0x01, -2999.9}, /* 1 */
        {0x00, 0x00, -3000.0}, /* 0: most negative encodable value */
        {0xFF, 0xFF, 3553.5},  /* 65535: most positive, for symmetry */
    };
    for (size_t i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
        const uint8_t p[8] = {0x01, 0x0E, 0x00, 0x00, v[i].hi, v[i].lo, 0x02, 0xEE};
        oamr_daly_rx_frame_t f = frame(0x90u, p);
        CHECK(oamr_daly_rx_on_frame(&d, &f, 10u + i) == OAMR_DALY_RX_DECODED);
        CHECK_NEAR(d.data.current_a, v[i].amps, 1e-3);
        CHECK(!d.data.confirmed);
    }
    CHECK(!oamr_daly_rx_usable(&d)); /* still locked */
}

static void maximum_cell_index(void)
{
    oamr_daly_rx_t d;
    oamr_daly_rx_config_t c = cfg();
    c.n_cells = OAMR_DALY_MAX_CELLS; /* 48, the decoder's upper bound */
    c.n_temps = OAMR_DALY_MAX_TEMPS; /* 16 */
    CHECK(oamr_daly_rx_init(&d, &c));
    c.n_cells = OAMR_DALY_MAX_CELLS + 1u;
    oamr_daly_rx_t d2;
    CHECK(!oamr_daly_rx_init(&d2, &c));

    /* 0x91: cell number equal to n_cells is accepted, n_cells + 1 is not. */
    const uint8_t mm[8] = {0x0D, 0x48, 48, 0x0C, 0xE4, 1, 0, 0};
    oamr_daly_rx_frame_t f = frame(0x91u, mm);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 1u) == OAMR_DALY_RX_DECODED);
    CHECK(d.data.cell_max_no == 48u && d.data.cell_min_no == 1u);
    const uint8_t mm_bad[8] = {0x0D, 0x48, 1, 0x0C, 0xE4, 49, 0, 0};
    f = frame(0x91u, mm_bad);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 2u) == OAMR_DALY_RX_MALFORMED);
    CHECK(d.data.cell_min_no == 1u); /* unchanged */

    /* 0x95: 48 cells are frames 1..16 [CALC]; frame 16 holds cells 46..48
     * (indices 45..47), the last one sets bit 47 of the mask. */
    const uint8_t last[8] = {16, 0x0C, 0x80, 0x0C, 0x81, 0x0C, 0x82, 0};
    f = frame(0x95u, last);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 3u) == OAMR_DALY_RX_DECODED);
    CHECK(d.data.cell_mv[45] == 0x0C80u && d.data.cell_mv[47] == 0x0C82u);
    CHECK(d.data.cell_seen_mask == (0x7ull << 45));
    const uint8_t past[8] = {17, 0x0C, 0x80, 0x0C, 0x81, 0x0C, 0x82, 0};
    f = frame(0x95u, past);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 4u) == OAMR_DALY_RX_MALFORMED);
    CHECK(d.data.cell_seen_mask == (0x7ull << 45));

    /* 0x96: 16 sensors are frames 1..3 [CALC]; frame 3 holds sensors 15, 16. */
    const uint8_t t3[8] = {3, 41, 42, 43, 44, 45, 46, 47};
    f = frame(0x96u, t3);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 5u) == OAMR_DALY_RX_DECODED);
    CHECK(d.data.cell_temp_c[14] == 1 && d.data.cell_temp_c[15] == 2);
    CHECK(d.data.temp_seen_mask == 0xC000u);
    const uint8_t t4[8] = {4, 41, 42, 43, 44, 45, 46, 47};
    f = frame(0x96u, t4);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 6u) == OAMR_DALY_RX_MALFORMED);
}

static void wrong_frame_numbering(void)
{
    oamr_daly_rx_t d;
    oamr_daly_rx_config_t c = cfg(); /* [CFG] 8 cells -> frames 1..3, 2 temps -> frame 1 */
    CHECK(oamr_daly_rx_init(&d, &c));
    const uint8_t bad_numbers[] = {0u, 4u, 5u, 0x80u, 0xFFu};
    for (size_t i = 0; i < sizeof(bad_numbers); i++) {
        const uint8_t p[8] = {bad_numbers[i], 0x0D, 0x00, 0x0D, 0x01, 0x0D, 0x02, 0};
        oamr_daly_rx_frame_t f = frame(0x95u, p);
        CHECK(oamr_daly_rx_on_frame(&d, &f, 1u) == OAMR_DALY_RX_MALFORMED);
        f = frame(0x96u, p);
        if (bad_numbers[i] == 0u || bad_numbers[i] >= 2u) {
            CHECK(oamr_daly_rx_on_frame(&d, &f, 1u) == OAMR_DALY_RX_MALFORMED);
        }
    }
    CHECK(d.data.cell_seen_mask == 0u && d.data.temp_seen_mask == 0u);
    CHECK(d.counters.malformed == 10u && d.counters.decoded == 0u);

    /* Frames out of order are placed by their number, not by arrival. */
    const uint8_t f3[8] = {3, 0x0C, 0x03, 0x0C, 0x04, 0x0C, 0x05, 0};
    const uint8_t f1[8] = {1, 0x0C, 0x00, 0x0C, 0x01, 0x0C, 0x02, 0};
    oamr_daly_rx_frame_t f = frame(0x95u, f3);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 2u) == OAMR_DALY_RX_DECODED);
    f = frame(0x95u, f1);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 3u) == OAMR_DALY_RX_DECODED);
    CHECK(d.data.cell_mv[0] == 0x0C00u && d.data.cell_mv[6] == 0x0C03u);
    CHECK(d.data.cell_mv[7] == 0x0C04u);
    CHECK(d.data.cell_seen_mask == 0xC7u); /* cells 1..3 and 7..8; frame 2 missing */
    /* A repeated frame number overwrites, it does not shift. */
    const uint8_t f1b[8] = {1, 0x0B, 0x00, 0x0B, 0x01, 0x0B, 0x02, 0};
    f = frame(0x95u, f1b);
    CHECK(oamr_daly_rx_on_frame(&d, &f, 4u) == OAMR_DALY_RX_DECODED);
    CHECK(d.data.cell_mv[0] == 0x0B00u && d.data.cell_mv[3] == 0u);
    CHECK(!oamr_daly_rx_usable(&d)); /* still locked */
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
        oamr_daly_rx_on_frame(&d, &f, (uint64_t)i);
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
    RUN(negative_current);
    RUN(maximum_cell_index);
    RUN(wrong_frame_numbering);
    RUN(fuzz);
    TEST_MAIN_END();
}
