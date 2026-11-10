/* SPDX-License-Identifier: MIT */
/*
 * TDK ICM-42688-P over SPI (SPI4 on the NUCLEO-H723ZG, Mode 0, at most
 * 8 MHz; TDK EV_ICM-42688-P board, P-03 item 26).
 *
 * Register facts from the ICM-42688-P datasheet (DS-000347): WHO_AM_I 0x75 =
 * 0x47; DEVICE_CONFIG 0x11 bit 0 soft reset (wait 1 ms); INT_CONFIG 0x14;
 * TEMP_DATA1 0x1D to GYRO_DATA_Z0 0x2A (big endian); INT_STATUS 0x2D bit 3
 * DATA_RDY_INT; PWR_MGMT0 0x4E; GYRO_CONFIG0 0x4F and ACCEL_CONFIG0 0x50
 * (ODR code 0111 = 200 Hz, FS_SEL 000 = 2000 dps / 16 g); INT_CONFIG1 0x64
 * bit 4 INT_ASYNC_RESET must be cleared for proper INT1 operation;
 * INT_SOURCE0 0x65 bit 3 UI_DRDY_INT1_EN; REG_BANK_SEL 0x76. Sensitivity
 * 16.4 LSB/(deg/s) and 2048 LSB/g at full scale 0; temperature
 * TEMP_DATA / 132.48 + 25 degC. With the default invalid-code setting the
 * sensor reports -32768 for an invalid sample.
 *
 * Sample rule (I8 baseline): a failed read or an invalid code is never
 * returned as data. It is counted, and the consumer only ever receives each
 * valid sample once (oamr_icm_take_sample), so an old reading can never be
 * published again as a new one.
 */
#ifndef OAMR_ICM42688_H
#define OAMR_ICM42688_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "oamr/imu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OAMR_ICM_WHO_AM_I_VALUE 0x47u

typedef struct {
    /* One SPI transaction with chip select asserted for its whole length. */
    bool (*transfer)(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len);
    void (*delay_us)(void *ctx, uint32_t us);
    void *ctx;
} oamr_spi_t;

typedef enum {
    OAMR_ICM_OK = 0,
    OAMR_ICM_BUS_ERROR,
    OAMR_ICM_WRONG_ID,
    OAMR_ICM_CONFIG_MISMATCH
} oamr_icm_result_t;

typedef struct {
    oamr_spi_t spi;
    bool configured;
    uint8_t who_am_i;
    oamr_imu_sample_t latest;     /* last attempted read, valid flag included */
    bool latest_taken;
    oamr_imu_counters_t counters;
    uint32_t seq;
} oamr_icm_t;

oamr_icm_result_t oamr_icm_init(oamr_icm_t *icm, const oamr_spi_t *spi);
/* Read one sample; stamp_us is the INT1 edge time (or poll time). */
void oamr_icm_read_sample(oamr_icm_t *icm, uint64_t stamp_us, oamr_imu_stamp_source_t src);
/* Polling path while INT1 is unassigned: reads INT_STATUS and, if data is
 * ready, the sample. Returns true if a read was attempted. */
bool oamr_icm_poll(oamr_icm_t *icm, uint64_t now_us);
/* Returns true and copies the sample only for a new, valid, not yet taken sample. */
bool oamr_icm_take_sample(oamr_icm_t *icm, oamr_imu_sample_t *out);
/* Healthy: configured, a valid sample within max_age_us, and fewer than
 * max_consecutive_bad failures in a row. */
bool oamr_icm_healthy(const oamr_icm_t *icm, uint64_t now_us, uint64_t max_age_us,
                      uint32_t max_consecutive_bad);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_ICM42688_H */
