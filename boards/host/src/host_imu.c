/* SPDX-License-Identifier: MIT */
/*
 * Fake ICM-42688-P register file for the host port: answers the real
 * adapter (firmware/sensor_bridge) over oamr_spi_t, produces a level,
 * motionless sample at the 200 Hz ODR, and can inject the sensor's invalid
 * code (-32768), a dead bus or an absent chip.
 */
#include "host_board.h"

#include <string.h>
#include <time.h>

#define REG_TEMP_DATA1 0x1Du
#define REG_INT_STATUS 0x2Du
#define REG_WHO_AM_I 0x75u
#define DATA_RDY_INT 0x08u

static void put16(host_imu_t *imu, uint8_t reg, int16_t v)
{
    imu->regs[reg] = (uint8_t)((uint16_t)v >> 8);
    imu->regs[reg + 1u] = (uint8_t)((uint16_t)v & 0xFFu);
}

void host_imu_init(host_imu_t *imu)
{
    memset(imu, 0, sizeof(*imu));
    imu->regs[REG_WHO_AM_I] = 0x47u;
    /* Reset values the adapter reads back and modifies (datasheet DS-000347). */
    imu->regs[0x64] = 0x10u;
    imu->regs[0x65] = 0x10u;
    imu->regs[0x4F] = 0x06u;
    imu->regs[0x50] = 0x06u;
    imu->period_us = 5000u;
}

static bool transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    host_imu_t *imu = (host_imu_t *)ctx;
    if (imu->spi_fail || len == 0u) {
        return false;
    }
    const uint8_t addr = tx[0] & 0x7Fu;
    if (tx[0] & 0x80u) {
        rx[0] = 0;
        for (size_t i = 1; i < len; i++) {
            const uint8_t r = (uint8_t)((addr + i - 1u) & 0x7Fu);
            rx[i] = (r == REG_WHO_AM_I && imu->absent) ? 0u : imu->regs[r];
        }
        if (addr == REG_INT_STATUS) {
            imu->regs[REG_INT_STATUS] = 0;  /* read clears */
        }
    } else if (len >= 2u && addr != 0x11u) { /* 0x11 DEVICE_CONFIG soft reset: no state */
        imu->regs[addr] = tx[1];
    }
    return true;
}

static void delay(void *ctx, uint32_t us)
{
    (void)ctx;
    struct timespec ts = {0, (long)us * 1000L};
    nanosleep(&ts, NULL);
}

oamr_spi_t host_imu_spi(host_imu_t *imu)
{
    oamr_spi_t s = {transfer, delay, imu};
    return s;
}

void host_imu_step(host_imu_t *imu, uint64_t now_us)
{
    if (now_us < imu->next_sample_us) {
        return;
    }
    imu->next_sample_us = now_us + imu->period_us;
    const int16_t bad = (int16_t)-32768;
    /* +-16 g, 2048 LSB/g: 1 g on z. Temperature 25 degC = 0 LSB. */
    put16(imu, REG_TEMP_DATA1, 0);
    put16(imu, 0x1F, imu->inject_invalid ? bad : 0);
    put16(imu, 0x21, 0);
    put16(imu, 0x23, 2048);
    put16(imu, 0x25, 0);
    put16(imu, 0x27, 0);
    put16(imu, 0x29, 0);
    imu->regs[REG_INT_STATUS] = DATA_RDY_INT;
}
