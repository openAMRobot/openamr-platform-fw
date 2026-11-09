// SPDX-License-Identifier: MIT
// Fake ICM-42688-P register file behind an oamr_spi_t.
#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

extern "C" {
#include "oamr/icm42688.h"
}

struct FakeSpi {
    std::array<uint8_t, 128> regs{};
    bool fail = false;
    bool ignore_writes = false;
    std::vector<std::pair<uint8_t, uint8_t>> writes;
    uint32_t delayed_us = 0;

    FakeSpi()
    {
        regs[0x75] = 0x47;
        regs[0x64] = 0x10;  // INT_ASYNC_RESET set at reset
        regs[0x65] = 0x10;
        regs[0x4F] = 0x06;
        regs[0x50] = 0x06;
    }
    static bool transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
    {
        auto *s = static_cast<FakeSpi *>(ctx);
        if (s->fail) {
            return false;
        }
        const uint8_t addr = tx[0] & 0x7F;
        if (tx[0] & 0x80) {
            rx[0] = 0;
            for (size_t i = 1; i < len; i++) {
                rx[i] = s->regs[(addr + i - 1) & 0x7F];
            }
            if (addr == 0x2D) s->regs[0x2D] = 0;  // read clears
        } else {
            s->writes.push_back({addr, tx[1]});
            if (!s->ignore_writes && addr != 0x11) s->regs[addr] = tx[1];
        }
        return true;
    }
    static void delay(void *ctx, uint32_t us) { static_cast<FakeSpi *>(ctx)->delayed_us += us; }
    oamr_spi_t spi() { return oamr_spi_t{&FakeSpi::transfer, &FakeSpi::delay, this}; }
    void put16(uint8_t reg, int16_t v)
    {
        regs[reg] = static_cast<uint8_t>(static_cast<uint16_t>(v) >> 8);
        regs[reg + 1] = static_cast<uint8_t>(v & 0xFF);
    }
    void sample(int16_t temp, int16_t ax, int16_t ay, int16_t az, int16_t gx, int16_t gy, int16_t gz)
    {
        put16(0x1D, temp);
        put16(0x1F, ax);
        put16(0x21, ay);
        put16(0x23, az);
        put16(0x25, gx);
        put16(0x27, gy);
        put16(0x29, gz);
        regs[0x2D] = 0x08;
    }
};
