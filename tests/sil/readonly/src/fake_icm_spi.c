/* SPDX-License-Identifier: MIT */
#include "fake_icm_spi.h"

#include <string.h>

#include "oamr/icm42688.h"

void oamr_sil_icm_fake_init(oamr_sil_icm_fake_t *f, uint8_t who_am_i)
{
    memset(f, 0, sizeof(*f));
    f->regs[OAMR_ICM_REG_WHO_AM_I] = who_am_i;
}

void oamr_sil_icm_fake_data(oamr_sil_icm_fake_t *f, const uint8_t raw[14])
{
    memcpy(&f->regs[OAMR_ICM_REG_TEMP_DATA1], raw, 14u);
    f->regs[OAMR_ICM_REG_INT_STATUS] = (uint8_t)(f->regs[OAMR_ICM_REG_INT_STATUS] | OAMR_ICM_DATA_RDY_INT);
}

bool oamr_sil_icm_fake_transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    oamr_sil_icm_fake_t *f = (oamr_sil_icm_fake_t *)ctx;
    f->transfers++;
    if (f->bus_down || len < 2u) {
        return false;
    }
    const uint8_t reg = (uint8_t)(tx[0] & 0x7Fu);
    if ((size_t)reg + len - 1u > sizeof(f->regs)) {
        return false;
    }
    rx[0] = 0u;
    if ((tx[0] & OAMR_ICM_SPI_READ) != 0u) {
        memcpy(&rx[1], &f->regs[reg], len - 1u);
        if (reg == OAMR_ICM_REG_INT_STATUS) {
            f->regs[OAMR_ICM_REG_INT_STATUS] = 0u; /* read clears, as on the part */
        }
        return true;
    }
    if (reg == OAMR_ICM_REG_DEVICE_CONFIG && (tx[1] & OAMR_ICM_SOFT_RESET) != 0u) {
        return true; /* soft reset: register file kept, WHO_AM_I unchanged */
    }
    memcpy(&f->regs[reg], &tx[1], len - 1u);
    return true;
}

void oamr_sil_icm_fake_delay(void *ctx, uint32_t us)
{
    (void)ctx;
    (void)us;
}
