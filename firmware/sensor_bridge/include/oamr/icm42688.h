/* SPDX-License-Identifier: MIT */
/*
 * TDK ICM-42688-P over SPI (SPI4 on the NUCLEO-H723ZG, Mode 0, at most
 * 8 MHz; TDK EV_ICM-42688-P board, P-03 item 26).
 *
 * Adapted from platform-fw #46 (firmware/sensor_bridge/icm42688.c). Changes:
 * full-scale ranges and output data rate are caller parameters with no
 * default (an unset value leaves the driver NOT_CONFIGURED); the sample type
 * is local to this module so it builds without the #46 core headers; time
 * stamps use the 32-bit wrapping microsecond clock of oamr/freshness.h; the
 * scaling and temperature conversions are pure functions with tests; a NULL
 * transfer or delay hook is rejected instead of called.
 *
 * Register facts, ICM-42688-P datasheet DS-000347 (user bank 0):
 *   DEVICE_CONFIG 0x11 bit 0 SOFT_RESET_CONFIG (wait 1 ms after),
 *   INT_CONFIG 0x14, TEMP_DATA1 0x1D .. GYRO_DATA_Z0 0x2A (big endian),
 *   INT_STATUS 0x2D bit 3 DATA_RDY_INT, PWR_MGMT0 0x4E,
 *   GYRO_CONFIG0 0x4F and ACCEL_CONFIG0 0x50 (FS_SEL bits 7:5, ODR bits 3:0),
 *   INT_CONFIG1 0x64 bit 4 INT_ASYNC_RESET (clear for INT1 operation),
 *   INT_SOURCE0 0x65 bit 3 UI_DRDY_INT1_EN, WHO_AM_I 0x75 = 0x47,
 *   REG_BANK_SEL 0x76. No register write within 200 us after the gyro is
 *   switched on. Temperature degC = TEMP_DATA / 132.48 + 25. With the default
 *   invalid-code setting the sensor reports -32768 for an invalid sample.
 *   Gyro sensitivity 16.4 LSB/(deg/s) at FS_SEL 0 (2000 dps), doubling per
 *   FS_SEL step; accel 2048 LSB/g at FS_SEL 0 (16 g), doubling per step.
 *
 * Sample rule (I8 baseline): a failed read or an invalid code is never
 * returned as data. It is counted, and each valid sample is handed out once
 * (oamr_icm_take_sample), so an old reading is never published as new.
 * Axes are in the sensor frame; no remap (imu_link carries the rotation).
 */
#ifndef OAMR_ICM42688_H
#define OAMR_ICM42688_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Register map, user bank 0 (DS-000347). */
#define OAMR_ICM_REG_DEVICE_CONFIG 0x11u
#define OAMR_ICM_REG_INT_CONFIG 0x14u
#define OAMR_ICM_REG_TEMP_DATA1 0x1Du
#define OAMR_ICM_REG_INT_STATUS 0x2Du
#define OAMR_ICM_REG_PWR_MGMT0 0x4Eu
#define OAMR_ICM_REG_GYRO_CONFIG0 0x4Fu
#define OAMR_ICM_REG_ACCEL_CONFIG0 0x50u
#define OAMR_ICM_REG_INT_CONFIG1 0x64u
#define OAMR_ICM_REG_INT_SOURCE0 0x65u
#define OAMR_ICM_REG_WHO_AM_I 0x75u
#define OAMR_ICM_REG_BANK_SEL 0x76u

#define OAMR_ICM_SPI_READ 0x80u
#define OAMR_ICM_WHO_AM_I_VALUE 0x47u
#define OAMR_ICM_SOFT_RESET 0x01u
#define OAMR_ICM_INT1_PULSED_PUSH_PULL_ACTIVE_HIGH 0x03u
#define OAMR_ICM_INT_ASYNC_RESET 0x10u
#define OAMR_ICM_UI_DRDY_INT1_EN 0x08u
#define OAMR_ICM_DATA_RDY_INT 0x08u
#define OAMR_ICM_PWR_GYRO_ACCEL_LOW_NOISE 0x0Fu
#define OAMR_ICM_INVALID_CODE (-32768)
#define OAMR_ICM_SOFT_RESET_WAIT_US 1000u
#define OAMR_ICM_GYRO_ON_WAIT_US 200u

/* GYRO_CONFIG0 FS_SEL (bits 7:5). 0 is reserved here for "unset". */
typedef enum {
    OAMR_ICM_GYRO_FS_UNSET = 0,
    OAMR_ICM_GYRO_FS_2000DPS = 1, /* FS_SEL 0 */
    OAMR_ICM_GYRO_FS_1000DPS = 2,
    OAMR_ICM_GYRO_FS_500DPS = 3,
    OAMR_ICM_GYRO_FS_250DPS = 4,
    OAMR_ICM_GYRO_FS_125DPS = 5,
    OAMR_ICM_GYRO_FS_62_5DPS = 6,
    OAMR_ICM_GYRO_FS_31_25DPS = 7,
    OAMR_ICM_GYRO_FS_15_625DPS = 8 /* FS_SEL 7 */
} oamr_icm_gyro_fs_t;

/* ACCEL_CONFIG0 FS_SEL (bits 7:5). */
typedef enum {
    OAMR_ICM_ACCEL_FS_UNSET = 0,
    OAMR_ICM_ACCEL_FS_16G = 1, /* FS_SEL 0 */
    OAMR_ICM_ACCEL_FS_8G = 2,
    OAMR_ICM_ACCEL_FS_4G = 3,
    OAMR_ICM_ACCEL_FS_2G = 4   /* FS_SEL 3 */
} oamr_icm_accel_fs_t;

typedef struct {
    /* One SPI transaction with chip select asserted for its whole length. */
    bool (*transfer)(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len);
    void (*delay_us)(void *ctx, uint32_t us);
    void *ctx;
} oamr_icm_spi_t;

typedef struct {
    oamr_icm_gyro_fs_t gyro_fs;   /* no default. TODO(Evans) */
    oamr_icm_accel_fs_t accel_fs; /* no default. TODO(Evans) */
    uint8_t odr_code;             /* ODR bits 3:0 for gyro and accel, 1..15; 0 = unset.
                                   * 0x07 is 200 Hz (DS-000347). TODO(Evans) */
} oamr_icm_config_t;

typedef enum {
    OAMR_ICM_OK = 0,
    OAMR_ICM_NOT_CONFIGURED,
    OAMR_ICM_BUS_ERROR,
    OAMR_ICM_WRONG_ID,
    OAMR_ICM_CONFIG_MISMATCH
} oamr_icm_result_t;

typedef enum {
    OAMR_ICM_STAMP_INT1 = 1, /* time stamp taken at the INT1 data-ready edge */
    OAMR_ICM_STAMP_POLL = 2  /* time stamp taken when polling found data ready */
} oamr_icm_stamp_source_t;

typedef struct {
    bool valid;
    uint32_t seq;          /* increments for every attempted read */
    uint32_t stamp_us;     /* caller's monotonic clock, wraps at 2^32 */
    oamr_icm_stamp_source_t stamp_source;
    float gyro_rad_s[3];   /* sensor frame */
    float accel_m_s2[3];   /* sensor frame */
    float temperature_c;
} oamr_icm_sample_t;

typedef struct {
    uint32_t samples_ok;
    uint32_t read_failures;   /* bus transfer failed */
    uint32_t invalid_samples; /* sensor returned its invalid code */
    uint32_t consecutive_bad;
} oamr_icm_counters_t;

typedef struct {
    oamr_icm_spi_t spi;
    oamr_icm_config_t cfg;
    bool configured;
    uint8_t who_am_i;
    oamr_icm_sample_t latest;
    bool latest_taken;
    oamr_icm_counters_t counters;
    uint32_t seq;
} oamr_icm_t;

/* Pure helpers. Return 0 for an unset or out-of-range selection. */
float oamr_icm_gyro_lsb_per_dps(oamr_icm_gyro_fs_t fs);
float oamr_icm_accel_lsb_per_g(oamr_icm_accel_fs_t fs);
uint8_t oamr_icm_gyro_config0(oamr_icm_gyro_fs_t fs, uint8_t odr_code);
uint8_t oamr_icm_accel_config0(oamr_icm_accel_fs_t fs, uint8_t odr_code);
float oamr_icm_temp_c(int16_t raw);
float oamr_icm_gyro_rad_s(int16_t raw, oamr_icm_gyro_fs_t fs);
float oamr_icm_accel_m_s2(int16_t raw, oamr_icm_accel_fs_t fs);
bool oamr_icm_config_valid(const oamr_icm_config_t *cfg);
/* Decode the 14 bytes read from TEMP_DATA1. Returns false (and leaves *out
 * untouched) if any word holds the invalid code. */
bool oamr_icm_decode(const uint8_t raw[14], const oamr_icm_config_t *cfg, oamr_icm_sample_t *out);

/* Device access through the caller's SPI hook. */
oamr_icm_result_t oamr_icm_init(oamr_icm_t *icm, const oamr_icm_spi_t *spi,
                                const oamr_icm_config_t *cfg);
void oamr_icm_read_sample(oamr_icm_t *icm, uint32_t stamp_us, oamr_icm_stamp_source_t src);
/* Polling path while INT1 is not wired: reads INT_STATUS and, if data is
 * ready, the sample. Returns true if a sample read was attempted or the
 * status read failed. */
bool oamr_icm_poll(oamr_icm_t *icm, uint32_t now_us);
/* Copies the sample only if it is new, valid and not yet taken. */
bool oamr_icm_take_sample(oamr_icm_t *icm, oamr_icm_sample_t *out);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_ICM42688_H */
