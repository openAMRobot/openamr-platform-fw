/* SPDX-License-Identifier: MIT */
#include "oamr/icm42688.h"

#include <string.h>

#define REG_DEVICE_CONFIG 0x11u
#define REG_INT_CONFIG 0x14u
#define REG_TEMP_DATA1 0x1Du
#define REG_INT_STATUS 0x2Du
#define REG_PWR_MGMT0 0x4Eu
#define REG_GYRO_CONFIG0 0x4Fu
#define REG_ACCEL_CONFIG0 0x50u
#define REG_INT_CONFIG1 0x64u
#define REG_INT_SOURCE0 0x65u
#define REG_WHO_AM_I 0x75u
#define REG_BANK_SEL 0x76u

#define SPI_READ 0x80u
#define SOFT_RESET 0x01u
#define INT1_PULSED_PUSH_PULL_ACTIVE_HIGH 0x03u
#define INT_ASYNC_RESET 0x10u
#define UI_DRDY_INT1_EN 0x08u
#define DATA_RDY_INT 0x08u
#define ODR_200HZ 0x07u
#define FS_SEL_0 0x00u                       /* 2000 dps, 16 g */
#define PWR_GYRO_ACCEL_LOW_NOISE 0x0Fu
#define INVALID_CODE (-32768)

#define GYRO_LSB_PER_DPS 16.4f
#define ACCEL_LSB_PER_G 2048.0f
#define TEMP_LSB_PER_C 132.48f
#define DEG_TO_RAD 0.017453292519943295f
#define STANDARD_GRAVITY 9.80665f

static bool read_regs(oamr_icm_t *icm, uint8_t reg, uint8_t *out, size_t n)
{
    uint8_t tx[16] = {0};
    uint8_t rx[16] = {0};
    if (n + 1u > sizeof(tx)) {
        return false;
    }
    tx[0] = (uint8_t)(reg | SPI_READ);
    if (!icm->spi.transfer(icm->spi.ctx, tx, rx, n + 1u)) {
        return false;
    }
    memcpy(out, &rx[1], n);
    return true;
}

static bool write_reg(oamr_icm_t *icm, uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = {(uint8_t)(reg & 0x7Fu), value};
    uint8_t rx[2];
    return icm->spi.transfer(icm->spi.ctx, tx, rx, 2u);
}

oamr_icm_result_t oamr_icm_init(oamr_icm_t *icm, const oamr_spi_t *spi)
{
    memset(icm, 0, sizeof(*icm));
    icm->spi = *spi;
    icm->latest_taken = true;

    if (!write_reg(icm, REG_BANK_SEL, 0u) || !read_regs(icm, REG_WHO_AM_I, &icm->who_am_i, 1u)) {
        return OAMR_ICM_BUS_ERROR;
    }
    if (icm->who_am_i != OAMR_ICM_WHO_AM_I_VALUE) {
        return OAMR_ICM_WRONG_ID;
    }
    if (!write_reg(icm, REG_DEVICE_CONFIG, SOFT_RESET)) {
        return OAMR_ICM_BUS_ERROR;
    }
    icm->spi.delay_us(icm->spi.ctx, 1000u);
    uint8_t id = 0;
    if (!write_reg(icm, REG_BANK_SEL, 0u) || !read_regs(icm, REG_WHO_AM_I, &id, 1u)) {
        return OAMR_ICM_BUS_ERROR;
    }
    if (id != OAMR_ICM_WHO_AM_I_VALUE) {
        return OAMR_ICM_WRONG_ID;
    }

    uint8_t int_config1 = 0;
    if (!read_regs(icm, REG_INT_CONFIG1, &int_config1, 1u)) {
        return OAMR_ICM_BUS_ERROR;
    }
    int_config1 = (uint8_t)(int_config1 & ~INT_ASYNC_RESET);

    const uint8_t gyro = (uint8_t)(FS_SEL_0 | ODR_200HZ);
    const uint8_t accel = (uint8_t)(FS_SEL_0 | ODR_200HZ);
    const struct {
        uint8_t reg;
        uint8_t value;
    } cfg[] = {
        {REG_INT_CONFIG, INT1_PULSED_PUSH_PULL_ACTIVE_HIGH},
        {REG_INT_CONFIG1, int_config1},
        {REG_INT_SOURCE0, UI_DRDY_INT1_EN},
        {REG_GYRO_CONFIG0, gyro},
        {REG_ACCEL_CONFIG0, accel},
        {REG_PWR_MGMT0, PWR_GYRO_ACCEL_LOW_NOISE},
    };
    for (size_t i = 0; i < sizeof(cfg) / sizeof(cfg[0]); i++) {
        if (!write_reg(icm, cfg[i].reg, cfg[i].value)) {
            return OAMR_ICM_BUS_ERROR;
        }
    }
    /* Sensors start after PWR_MGMT0; wait before the next access. */
    icm->spi.delay_us(icm->spi.ctx, 200u);
    for (size_t i = 0; i < sizeof(cfg) / sizeof(cfg[0]); i++) {
        uint8_t v = 0;
        if (!read_regs(icm, cfg[i].reg, &v, 1u)) {
            return OAMR_ICM_BUS_ERROR;
        }
        if (v != cfg[i].value) {
            return OAMR_ICM_CONFIG_MISMATCH;
        }
    }
    icm->configured = true;
    return OAMR_ICM_OK;
}

static int16_t be16(const uint8_t *p)
{
    return (int16_t)(uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void mark_bad(oamr_icm_t *icm)
{
    icm->latest.valid = false;
    icm->latest_taken = true;
    icm->counters.consecutive_bad++;
}

void oamr_icm_read_sample(oamr_icm_t *icm, uint64_t stamp_us, oamr_imu_stamp_source_t src)
{
    uint8_t raw[14];
    oamr_imu_sample_t *s = &icm->latest;
    s->seq = ++icm->seq;
    s->stamp_us = stamp_us;
    s->stamp_source = src;
    if (!icm->configured || !read_regs(icm, REG_TEMP_DATA1, raw, sizeof(raw))) {
        icm->counters.read_failures++;
        mark_bad(icm);
        return;
    }
    int16_t v[7];
    for (size_t i = 0; i < 7; i++) {
        v[i] = be16(&raw[2u * i]);
    }
    /* v[0] temperature, v[1..3] accel, v[4..6] gyro */
    for (size_t i = 0; i < 7; i++) {
        if (v[i] == INVALID_CODE) {
            icm->counters.invalid_samples++;
            mark_bad(icm);
            return;
        }
    }
    s->temperature_c = (float)v[0] / TEMP_LSB_PER_C + 25.0f;
    for (size_t i = 0; i < 3; i++) {
        s->accel_m_s2[i] = (float)v[1 + i] / ACCEL_LSB_PER_G * STANDARD_GRAVITY;
        s->gyro_rad_s[i] = (float)v[4 + i] / GYRO_LSB_PER_DPS * DEG_TO_RAD;
    }
    s->valid = true;
    icm->latest_taken = false;
    icm->counters.samples_ok++;
    icm->counters.consecutive_bad = 0;
}

bool oamr_icm_poll(oamr_icm_t *icm, uint64_t now_us)
{
    uint8_t status = 0;
    if (!read_regs(icm, REG_INT_STATUS, &status, 1u)) {
        icm->seq++;
        icm->counters.read_failures++;
        mark_bad(icm);
        return true;
    }
    if ((status & DATA_RDY_INT) == 0u) {
        return false;
    }
    oamr_icm_read_sample(icm, now_us, OAMR_IMU_STAMP_POLL);
    return true;
}

bool oamr_icm_take_sample(oamr_icm_t *icm, oamr_imu_sample_t *out)
{
    if (!icm->latest.valid || icm->latest_taken) {
        return false;
    }
    *out = icm->latest;
    icm->latest_taken = true;
    return true;
}

bool oamr_icm_healthy(const oamr_icm_t *icm, uint64_t now_us, uint64_t max_age_us,
                      uint32_t max_consecutive_bad)
{
    return icm->configured && icm->latest.valid &&
           oamr_imu_sample_age_us(&icm->latest, now_us) < max_age_us &&
           icm->counters.consecutive_bad < max_consecutive_bad;
}
