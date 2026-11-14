/* SPDX-License-Identifier: MIT */
#include "oamr/icm42688.h"

#include <string.h>

#define GYRO_LSB_PER_DPS_FS0 16.4f
#define ACCEL_LSB_PER_G_FS0 2048.0f
#define TEMP_LSB_PER_C 132.48f
#define TEMP_OFFSET_C 25.0f
#define DEG_TO_RAD 0.017453292519943295f
#define STANDARD_GRAVITY 9.80665f

float oamr_icm_gyro_lsb_per_dps(oamr_icm_gyro_fs_t fs)
{
    if (fs < OAMR_ICM_GYRO_FS_2000DPS || fs > OAMR_ICM_GYRO_FS_15_625DPS) {
        return 0.0f;
    }
    float v = GYRO_LSB_PER_DPS_FS0;
    for (int i = (int)OAMR_ICM_GYRO_FS_2000DPS; i < (int)fs; i++) {
        v *= 2.0f;
    }
    return v;
}

float oamr_icm_accel_lsb_per_g(oamr_icm_accel_fs_t fs)
{
    if (fs < OAMR_ICM_ACCEL_FS_16G || fs > OAMR_ICM_ACCEL_FS_2G) {
        return 0.0f;
    }
    float v = ACCEL_LSB_PER_G_FS0;
    for (int i = (int)OAMR_ICM_ACCEL_FS_16G; i < (int)fs; i++) {
        v *= 2.0f;
    }
    return v;
}

static bool odr_valid(uint8_t odr)
{
    return odr >= 1u && odr <= 15u;
}

uint8_t oamr_icm_gyro_config0(oamr_icm_gyro_fs_t fs, uint8_t odr_code)
{
    if (oamr_icm_gyro_lsb_per_dps(fs) == 0.0f || !odr_valid(odr_code)) {
        return 0u;
    }
    return (uint8_t)((((unsigned)fs - 1u) << 5) | odr_code);
}

uint8_t oamr_icm_accel_config0(oamr_icm_accel_fs_t fs, uint8_t odr_code)
{
    if (oamr_icm_accel_lsb_per_g(fs) == 0.0f || !odr_valid(odr_code)) {
        return 0u;
    }
    return (uint8_t)((((unsigned)fs - 1u) << 5) | odr_code);
}

float oamr_icm_temp_c(int16_t raw)
{
    return (float)raw / TEMP_LSB_PER_C + TEMP_OFFSET_C;
}

float oamr_icm_gyro_rad_s(int16_t raw, oamr_icm_gyro_fs_t fs)
{
    const float lsb = oamr_icm_gyro_lsb_per_dps(fs);
    return lsb == 0.0f ? 0.0f : (float)raw / lsb * DEG_TO_RAD;
}

float oamr_icm_accel_m_s2(int16_t raw, oamr_icm_accel_fs_t fs)
{
    const float lsb = oamr_icm_accel_lsb_per_g(fs);
    return lsb == 0.0f ? 0.0f : (float)raw / lsb * STANDARD_GRAVITY;
}

bool oamr_icm_config_valid(const oamr_icm_config_t *cfg)
{
    return cfg != NULL && oamr_icm_gyro_config0(cfg->gyro_fs, cfg->odr_code) != 0u &&
           oamr_icm_accel_config0(cfg->accel_fs, cfg->odr_code) != 0u;
}

static int16_t be16(const uint8_t *p)
{
    return (int16_t)(uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

bool oamr_icm_decode(const uint8_t raw[14], const oamr_icm_config_t *cfg, oamr_icm_sample_t *out)
{
    int16_t v[7];
    for (size_t i = 0; i < 7u; i++) {
        v[i] = be16(&raw[2u * i]);
        if (v[i] == OAMR_ICM_INVALID_CODE) {
            return false;
        }
    }
    /* v[0] temperature, v[1..3] accel X Y Z, v[4..6] gyro X Y Z */
    out->temperature_c = oamr_icm_temp_c(v[0]);
    for (size_t i = 0; i < 3u; i++) {
        out->accel_m_s2[i] = oamr_icm_accel_m_s2(v[1 + i], cfg->accel_fs);
        out->gyro_rad_s[i] = oamr_icm_gyro_rad_s(v[4 + i], cfg->gyro_fs);
    }
    return true;
}

static bool read_regs(oamr_icm_t *icm, uint8_t reg, uint8_t *out, size_t n)
{
    uint8_t tx[16] = {0};
    uint8_t rx[16] = {0};
    if (n + 1u > sizeof(tx)) {
        return false;
    }
    tx[0] = (uint8_t)(reg | OAMR_ICM_SPI_READ);
    if (!icm->spi.transfer(icm->spi.ctx, tx, rx, n + 1u)) {
        return false;
    }
    memcpy(out, &rx[1], n);
    return true;
}

static bool write_reg(oamr_icm_t *icm, uint8_t reg, uint8_t value)
{
    const uint8_t tx[2] = {(uint8_t)(reg & 0x7Fu), value};
    uint8_t rx[2] = {0};
    return icm->spi.transfer(icm->spi.ctx, tx, rx, 2u);
}

oamr_icm_result_t oamr_icm_init(oamr_icm_t *icm, const oamr_icm_spi_t *spi,
                                const oamr_icm_config_t *cfg)
{
    memset(icm, 0, sizeof(*icm));
    icm->latest_taken = true;
    if (spi == NULL || spi->transfer == NULL || spi->delay_us == NULL || !oamr_icm_config_valid(cfg)) {
        return OAMR_ICM_NOT_CONFIGURED;
    }
    icm->spi = *spi;
    icm->cfg = *cfg;

    if (!write_reg(icm, OAMR_ICM_REG_BANK_SEL, 0u) ||
        !read_regs(icm, OAMR_ICM_REG_WHO_AM_I, &icm->who_am_i, 1u)) {
        return OAMR_ICM_BUS_ERROR;
    }
    if (icm->who_am_i != OAMR_ICM_WHO_AM_I_VALUE) {
        return OAMR_ICM_WRONG_ID;
    }
    if (!write_reg(icm, OAMR_ICM_REG_DEVICE_CONFIG, OAMR_ICM_SOFT_RESET)) {
        return OAMR_ICM_BUS_ERROR;
    }
    icm->spi.delay_us(icm->spi.ctx, OAMR_ICM_SOFT_RESET_WAIT_US);
    uint8_t id = 0u;
    if (!write_reg(icm, OAMR_ICM_REG_BANK_SEL, 0u) || !read_regs(icm, OAMR_ICM_REG_WHO_AM_I, &id, 1u)) {
        return OAMR_ICM_BUS_ERROR;
    }
    if (id != OAMR_ICM_WHO_AM_I_VALUE) {
        return OAMR_ICM_WRONG_ID;
    }
    uint8_t int_config1 = 0u;
    if (!read_regs(icm, OAMR_ICM_REG_INT_CONFIG1, &int_config1, 1u)) {
        return OAMR_ICM_BUS_ERROR;
    }
    int_config1 = (uint8_t)(int_config1 & (uint8_t)~OAMR_ICM_INT_ASYNC_RESET);

    const struct {
        uint8_t reg;
        uint8_t value;
    } seq[] = {
        {OAMR_ICM_REG_INT_CONFIG, OAMR_ICM_INT1_PULSED_PUSH_PULL_ACTIVE_HIGH},
        {OAMR_ICM_REG_INT_CONFIG1, int_config1},
        {OAMR_ICM_REG_INT_SOURCE0, OAMR_ICM_UI_DRDY_INT1_EN},
        {OAMR_ICM_REG_GYRO_CONFIG0, oamr_icm_gyro_config0(cfg->gyro_fs, cfg->odr_code)},
        {OAMR_ICM_REG_ACCEL_CONFIG0, oamr_icm_accel_config0(cfg->accel_fs, cfg->odr_code)},
        {OAMR_ICM_REG_PWR_MGMT0, OAMR_ICM_PWR_GYRO_ACCEL_LOW_NOISE},
    };
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
        if (!write_reg(icm, seq[i].reg, seq[i].value)) {
            return OAMR_ICM_BUS_ERROR;
        }
    }
    icm->spi.delay_us(icm->spi.ctx, OAMR_ICM_GYRO_ON_WAIT_US);
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
        uint8_t v = 0u;
        if (!read_regs(icm, seq[i].reg, &v, 1u)) {
            return OAMR_ICM_BUS_ERROR;
        }
        if (v != seq[i].value) {
            return OAMR_ICM_CONFIG_MISMATCH;
        }
    }
    icm->configured = true;
    return OAMR_ICM_OK;
}

static void mark_bad(oamr_icm_t *icm)
{
    icm->latest.valid = false;
    icm->latest_taken = true;
    icm->counters.consecutive_bad++;
}

void oamr_icm_read_sample(oamr_icm_t *icm, uint32_t stamp_us, oamr_icm_stamp_source_t src)
{
    uint8_t raw[14];
    oamr_icm_sample_t *s = &icm->latest;
    s->seq = ++icm->seq;
    s->stamp_us = stamp_us;
    s->stamp_source = src;
    if (!icm->configured || !read_regs(icm, OAMR_ICM_REG_TEMP_DATA1, raw, sizeof(raw))) {
        icm->counters.read_failures++;
        mark_bad(icm);
        return;
    }
    if (!oamr_icm_decode(raw, &icm->cfg, s)) {
        icm->counters.invalid_samples++;
        mark_bad(icm);
        return;
    }
    s->valid = true;
    icm->latest_taken = false;
    icm->counters.samples_ok++;
    icm->counters.consecutive_bad = 0u;
}

bool oamr_icm_poll(oamr_icm_t *icm, uint32_t now_us)
{
    if (!icm->configured) {
        return false;
    }
    uint8_t status = 0u;
    if (!read_regs(icm, OAMR_ICM_REG_INT_STATUS, &status, 1u)) {
        icm->latest.seq = ++icm->seq;
        icm->counters.read_failures++;
        mark_bad(icm);
        return true;
    }
    if ((status & OAMR_ICM_DATA_RDY_INT) == 0u) {
        return false;
    }
    oamr_icm_read_sample(icm, now_us, OAMR_ICM_STAMP_POLL);
    return true;
}

bool oamr_icm_take_sample(oamr_icm_t *icm, oamr_icm_sample_t *out)
{
    if (!icm->latest.valid || icm->latest_taken) {
        return false;
    }
    *out = icm->latest;
    icm->latest_taken = true;
    return true;
}
