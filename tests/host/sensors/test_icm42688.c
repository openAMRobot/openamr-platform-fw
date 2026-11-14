/* SPDX-License-Identifier: MIT */
#include <string.h>

#include "oamr/icm42688.h"
#include "test_harness.h"

/* Fake ICM-42688-P: a 128-byte register file behind the SPI hook. */
typedef struct {
    uint8_t regs[128];
    bool fail_all;
    int fail_after;    /* fail the n-th transfer from now (1-based); 0 = never */
    int transfers;
    bool ignore_writes_to;   /* simulate a register that does not stick */
    uint8_t ignored_reg;
    uint32_t delays_us;
} fake_t;

static bool fake_transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    fake_t *f = (fake_t *)ctx;
    f->transfers++;
    if (f->fail_all || (f->fail_after != 0 && f->transfers == f->fail_after)) {
        return false;
    }
    const uint8_t reg = (uint8_t)(tx[0] & 0x7Fu);
    if ((tx[0] & 0x80u) != 0u) {
        rx[0] = 0u;
        for (size_t i = 1; i < len; i++) {
            rx[i] = f->regs[(reg + i - 1u) & 0x7Fu];
        }
    } else {
        for (size_t i = 1; i < len; i++) {
            uint8_t r = (uint8_t)((reg + i - 1u) & 0x7Fu);
            if (f->ignore_writes_to && r == f->ignored_reg) {
                continue;
            }
            if (r == OAMR_ICM_REG_DEVICE_CONFIG) {
                continue; /* soft reset: self-clearing */
            }
            f->regs[r] = tx[i];
        }
    }
    return true;
}

static void fake_delay(void *ctx, uint32_t us)
{
    ((fake_t *)ctx)->delays_us += us;
}

static oamr_icm_config_t cfg_ok(void)
{
    oamr_icm_config_t c;
    c.gyro_fs = OAMR_ICM_GYRO_FS_2000DPS;
    c.accel_fs = OAMR_ICM_ACCEL_FS_16G;
    c.odr_code = 0x07u; /* test value */
    return c;
}

static void fake_init(fake_t *f, oamr_icm_spi_t *spi)
{
    memset(f, 0, sizeof(*f));
    f->regs[OAMR_ICM_REG_WHO_AM_I] = OAMR_ICM_WHO_AM_I_VALUE;
    f->regs[OAMR_ICM_REG_INT_CONFIG1] = 0x10u; /* reset value has INT_ASYNC_RESET set */
    spi->transfer = fake_transfer;
    spi->delay_us = fake_delay;
    spi->ctx = f;
}

static void put_be16(uint8_t *p, int16_t v)
{
    p[0] = (uint8_t)((uint16_t)v >> 8);
    p[1] = (uint8_t)((uint16_t)v & 0xFFu);
}

static void scaling(void)
{
    CHECK_NEAR(oamr_icm_gyro_lsb_per_dps(OAMR_ICM_GYRO_FS_2000DPS), 16.4, 1e-6);
    CHECK_NEAR(oamr_icm_gyro_lsb_per_dps(OAMR_ICM_GYRO_FS_250DPS), 131.2, 1e-4);
    CHECK_NEAR(oamr_icm_gyro_lsb_per_dps(OAMR_ICM_GYRO_FS_15_625DPS), 2099.2, 1e-3);
    CHECK(oamr_icm_gyro_lsb_per_dps(OAMR_ICM_GYRO_FS_UNSET) == 0.0f);
    CHECK(oamr_icm_gyro_lsb_per_dps((oamr_icm_gyro_fs_t)9) == 0.0f);
    CHECK_NEAR(oamr_icm_accel_lsb_per_g(OAMR_ICM_ACCEL_FS_16G), 2048.0, 1e-6);
    CHECK_NEAR(oamr_icm_accel_lsb_per_g(OAMR_ICM_ACCEL_FS_2G), 16384.0, 1e-6);
    CHECK(oamr_icm_accel_lsb_per_g(OAMR_ICM_ACCEL_FS_UNSET) == 0.0f);
    /* register encodings: FS_SEL in 7:5, ODR in 3:0 */
    CHECK(oamr_icm_gyro_config0(OAMR_ICM_GYRO_FS_2000DPS, 0x06u) == 0x06u);
    CHECK(oamr_icm_gyro_config0(OAMR_ICM_GYRO_FS_250DPS, 0x07u) == 0x67u);
    CHECK(oamr_icm_gyro_config0(OAMR_ICM_GYRO_FS_15_625DPS, 0x0Fu) == 0xEFu);
    CHECK(oamr_icm_accel_config0(OAMR_ICM_ACCEL_FS_2G, 0x07u) == 0x67u);
    CHECK(oamr_icm_gyro_config0(OAMR_ICM_GYRO_FS_2000DPS, 0u) == 0u);
    CHECK(oamr_icm_gyro_config0(OAMR_ICM_GYRO_FS_2000DPS, 16u) == 0u);
    /* temperature: raw/132.48 + 25 */
    CHECK_NEAR(oamr_icm_temp_c(0), 25.0, 1e-6);
    CHECK_NEAR(oamr_icm_temp_c(1325), 25.0 + 1325.0 / 132.48, 1e-4);
    CHECK_NEAR(oamr_icm_temp_c(-1325), 25.0 - 1325.0 / 132.48, 1e-4);
    /* 16.4 LSB = 1 deg/s */
    CHECK_NEAR(oamr_icm_gyro_rad_s(164, OAMR_ICM_GYRO_FS_2000DPS), 10.0 * 0.017453292519943295, 1e-6);
    CHECK_NEAR(oamr_icm_accel_m_s2(2048, OAMR_ICM_ACCEL_FS_16G), 9.80665, 1e-5);
    CHECK_NEAR(oamr_icm_accel_m_s2(-32767, OAMR_ICM_ACCEL_FS_16G), -32767.0 / 2048.0 * 9.80665, 1e-3);
}

static void config_required(void)
{
    fake_t f;
    oamr_icm_spi_t spi;
    fake_init(&f, &spi);
    oamr_icm_t icm;
    oamr_icm_config_t c = cfg_ok();
    c.gyro_fs = OAMR_ICM_GYRO_FS_UNSET;
    CHECK(oamr_icm_init(&icm, &spi, &c) == OAMR_ICM_NOT_CONFIGURED);
    c = cfg_ok();
    c.accel_fs = OAMR_ICM_ACCEL_FS_UNSET;
    CHECK(oamr_icm_init(&icm, &spi, &c) == OAMR_ICM_NOT_CONFIGURED);
    c = cfg_ok();
    c.odr_code = 0u;
    CHECK(oamr_icm_init(&icm, &spi, &c) == OAMR_ICM_NOT_CONFIGURED);
    c = cfg_ok();
    spi.transfer = NULL;
    CHECK(oamr_icm_init(&icm, &spi, &c) == OAMR_ICM_NOT_CONFIGURED);
    CHECK(f.transfers == 0);
    /* not configured: read is counted as failure, nothing to take */
    oamr_icm_read_sample(&icm, 5u, OAMR_ICM_STAMP_INT1);
    oamr_icm_sample_t s;
    CHECK(!oamr_icm_take_sample(&icm, &s));
    CHECK(icm.counters.read_failures == 1u);
    CHECK(!oamr_icm_poll(&icm, 5u));
}

static void init_ok_and_wrong_id(void)
{
    fake_t f;
    oamr_icm_spi_t spi;
    fake_init(&f, &spi);
    oamr_icm_t icm;
    oamr_icm_config_t c = cfg_ok();
    CHECK(oamr_icm_init(&icm, &spi, &c) == OAMR_ICM_OK);
    CHECK(f.regs[OAMR_ICM_REG_GYRO_CONFIG0] == 0x07u);
    CHECK(f.regs[OAMR_ICM_REG_ACCEL_CONFIG0] == 0x07u);
    CHECK(f.regs[OAMR_ICM_REG_PWR_MGMT0] == 0x0Fu);
    CHECK(f.regs[OAMR_ICM_REG_INT_SOURCE0] == 0x08u);
    CHECK((f.regs[OAMR_ICM_REG_INT_CONFIG1] & 0x10u) == 0u);
    CHECK(f.delays_us >= 1200u);

    fake_init(&f, &spi);
    f.regs[OAMR_ICM_REG_WHO_AM_I] = 0x6Fu;
    CHECK(oamr_icm_init(&icm, &spi, &c) == OAMR_ICM_WRONG_ID);
    CHECK(!icm.configured);

    fake_init(&f, &spi);
    f.fail_all = true;
    CHECK(oamr_icm_init(&icm, &spi, &c) == OAMR_ICM_BUS_ERROR);

    fake_init(&f, &spi);
    f.ignore_writes_to = true;
    f.ignored_reg = OAMR_ICM_REG_PWR_MGMT0;
    CHECK(oamr_icm_init(&icm, &spi, &c) == OAMR_ICM_CONFIG_MISMATCH);
    CHECK(!icm.configured);
}

static void read_take_once(void)
{
    fake_t f;
    oamr_icm_spi_t spi;
    fake_init(&f, &spi);
    oamr_icm_t icm;
    oamr_icm_config_t c = cfg_ok();
    CHECK(oamr_icm_init(&icm, &spi, &c) == OAMR_ICM_OK);
    uint8_t *d = &f.regs[OAMR_ICM_REG_TEMP_DATA1];
    put_be16(&d[0], 0);      /* 25 degC */
    put_be16(&d[2], 0);
    put_be16(&d[4], 0);
    put_be16(&d[6], 2048);   /* +1 g on Z */
    put_be16(&d[8], 164);    /* 10 deg/s on X */
    put_be16(&d[10], -164);
    put_be16(&d[12], 0);
    oamr_icm_sample_t s;
    CHECK(!oamr_icm_take_sample(&icm, &s)); /* nothing read yet */
    oamr_icm_read_sample(&icm, 1000u, OAMR_ICM_STAMP_INT1);
    CHECK(oamr_icm_take_sample(&icm, &s));
    CHECK(s.valid && s.seq == 1u && s.stamp_us == 1000u && s.stamp_source == OAMR_ICM_STAMP_INT1);
    CHECK_NEAR(s.temperature_c, 25.0, 1e-6);
    CHECK_NEAR(s.accel_m_s2[2], 9.80665, 1e-5);
    CHECK_NEAR(s.gyro_rad_s[0], 0.17453292, 1e-6);
    CHECK_NEAR(s.gyro_rad_s[1], -0.17453292, 1e-6);
    CHECK(!oamr_icm_take_sample(&icm, &s)); /* never handed out twice */

    /* invalid code -> no sample, counted */
    put_be16(&d[8], -32768);
    oamr_icm_read_sample(&icm, 2000u, OAMR_ICM_STAMP_INT1);
    CHECK(!oamr_icm_take_sample(&icm, &s));
    CHECK(icm.counters.invalid_samples == 1u);
    CHECK(icm.counters.consecutive_bad == 1u);

    /* bus failure -> no sample, counted */
    put_be16(&d[8], 0);
    f.fail_after = f.transfers + 1;
    oamr_icm_read_sample(&icm, 3000u, OAMR_ICM_STAMP_INT1);
    CHECK(!oamr_icm_take_sample(&icm, &s));
    CHECK(icm.counters.read_failures == 1u);
    CHECK(icm.counters.consecutive_bad == 2u);

    /* a good read after a bad one yields a new seq */
    oamr_icm_read_sample(&icm, 4000u, OAMR_ICM_STAMP_INT1);
    CHECK(oamr_icm_take_sample(&icm, &s));
    CHECK(s.seq == 4u && s.stamp_us == 4000u);
    CHECK(icm.counters.consecutive_bad == 0u);
}

static void poll_path(void)
{
    fake_t f;
    oamr_icm_spi_t spi;
    fake_init(&f, &spi);
    oamr_icm_t icm;
    oamr_icm_config_t c = cfg_ok();
    CHECK(oamr_icm_init(&icm, &spi, &c) == OAMR_ICM_OK);
    oamr_icm_sample_t s;
    f.regs[OAMR_ICM_REG_INT_STATUS] = 0u;
    CHECK(!oamr_icm_poll(&icm, 10u));
    CHECK(!oamr_icm_take_sample(&icm, &s));
    f.regs[OAMR_ICM_REG_INT_STATUS] = OAMR_ICM_DATA_RDY_INT;
    CHECK(oamr_icm_poll(&icm, 20u));
    CHECK(oamr_icm_take_sample(&icm, &s));
    CHECK(s.stamp_source == OAMR_ICM_STAMP_POLL && s.stamp_us == 20u);
    f.fail_after = f.transfers + 1;
    CHECK(oamr_icm_poll(&icm, 30u));
    CHECK(!oamr_icm_take_sample(&icm, &s));
    CHECK(icm.counters.read_failures == 1u);
}

int main(void)
{
    RUN(scaling);
    RUN(config_required);
    RUN(init_ok_and_wrong_id);
    RUN(read_take_once);
    RUN(poll_path);
    TEST_MAIN_END();
}
