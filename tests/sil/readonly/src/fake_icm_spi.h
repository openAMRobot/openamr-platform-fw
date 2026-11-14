/* SPDX-License-Identifier: MIT */
/*
 * Test double: ICM-42688-P register file behind the oamr_icm_spi_t hook of
 * firmware/sensor_bridge/icm42688.h. Lives under tests/ only. It models just
 * what the driver touches: reads return register contents, writes store them,
 * reading INT_STATUS clears the data-ready bit, and a "bus down" flag makes
 * every transfer fail. WHO_AM_I is whatever the harness sets (identity
 * mismatch scenario). No timing model.
 */
#ifndef OAMR_SIL_FAKE_ICM_SPI_H
#define OAMR_SIL_FAKE_ICM_SPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t regs[128];
    bool bus_down;
    uint32_t transfers;
} oamr_sil_icm_fake_t;

void oamr_sil_icm_fake_init(oamr_sil_icm_fake_t *f, uint8_t who_am_i);
/* Put 14 bytes at TEMP_DATA1 and raise data-ready. */
void oamr_sil_icm_fake_data(oamr_sil_icm_fake_t *f, const uint8_t raw[14]);
bool oamr_sil_icm_fake_transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len);
void oamr_sil_icm_fake_delay(void *ctx, uint32_t us);

#endif
