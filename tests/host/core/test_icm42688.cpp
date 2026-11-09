// SPDX-License-Identifier: MIT
// ICM-42688-P adapter against a fake SPI register file: identity check,
// 200 Hz configuration with read-back, sample conversion, invalid samples.
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

extern "C" {
#include "oamr/icm42688.h"
}

namespace {

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

class Icm : public ::testing::Test {
protected:
    void init_ok()
    {
        spi = fake.spi();
        ASSERT_EQ(oamr_icm_init(&icm, &spi), OAMR_ICM_OK);
    }
    FakeSpi fake;
    oamr_spi_t spi{};
    oamr_icm_t icm{};
};

TEST_F(Icm, WrongWhoAmIRejected)
{
    fake.regs[0x75] = 0x6A;
    spi = fake.spi();
    EXPECT_EQ(oamr_icm_init(&icm, &spi), OAMR_ICM_WRONG_ID);
    EXPECT_FALSE(icm.configured);
}

TEST_F(Icm, BusErrorRejected)
{
    fake.fail = true;
    spi = fake.spi();
    EXPECT_EQ(oamr_icm_init(&icm, &spi), OAMR_ICM_BUS_ERROR);
}

TEST_F(Icm, ConfiguresFor200HzAndInt1DataReady)
{
    init_ok();
    EXPECT_EQ(fake.regs[0x4F], 0x07);  // gyro 2000 dps, 200 Hz
    EXPECT_EQ(fake.regs[0x50], 0x07);  // accel 16 g, 200 Hz
    EXPECT_EQ(fake.regs[0x4E], 0x0F);  // gyro and accel low-noise
    EXPECT_EQ(fake.regs[0x65], 0x08);  // UI data ready on INT1
    EXPECT_EQ(fake.regs[0x64] & 0x10, 0);  // INT_ASYNC_RESET cleared
    EXPECT_EQ(fake.regs[0x14], 0x03);
    bool reset = false;
    for (auto &w : fake.writes) reset = reset || (w.first == 0x11 && w.second == 0x01);
    EXPECT_TRUE(reset);
    EXPECT_GE(fake.delayed_us, 1000u);
}

TEST_F(Icm, ConfigReadBackMismatchRejected)
{
    fake.ignore_writes = true;
    spi = fake.spi();
    EXPECT_EQ(oamr_icm_init(&icm, &spi), OAMR_ICM_CONFIG_MISMATCH);
    EXPECT_FALSE(icm.configured);
}

TEST_F(Icm, SampleConvertedAndTakenOnce)
{
    init_ok();
    fake.sample(0, 2048, 0, -2048, 164, 0, -164);
    oamr_icm_read_sample(&icm, 5000, OAMR_IMU_STAMP_INT1);
    oamr_imu_sample_t s{};
    ASSERT_TRUE(oamr_icm_take_sample(&icm, &s));
    EXPECT_TRUE(s.valid);
    EXPECT_EQ(s.stamp_us, 5000u);
    EXPECT_NEAR(s.accel_m_s2[0], 9.80665f, 1e-4);
    EXPECT_NEAR(s.accel_m_s2[2], -9.80665f, 1e-4);
    EXPECT_NEAR(s.gyro_rad_s[0], 10.0f * 0.0174533f, 1e-5);
    EXPECT_NEAR(s.temperature_c, 25.0f, 1e-5);
    EXPECT_FALSE(oamr_icm_take_sample(&icm, &s));  // never handed out twice
}

TEST_F(Icm, FailedReadIsNeverPublishedAndIsCounted)
{
    init_ok();
    fake.sample(0, 100, 100, 100, 100, 100, 100);
    oamr_icm_read_sample(&icm, 1000, OAMR_IMU_STAMP_INT1);
    oamr_imu_sample_t s{};
    ASSERT_TRUE(oamr_icm_take_sample(&icm, &s));
    // Next read fails: the old reading must not come back as a new sample.
    fake.fail = true;
    oamr_icm_read_sample(&icm, 6000, OAMR_IMU_STAMP_INT1);
    EXPECT_FALSE(oamr_icm_take_sample(&icm, &s));
    EXPECT_EQ(icm.counters.read_failures, 1u);
    EXPECT_FALSE(icm.latest.valid);
    EXPECT_FALSE(oamr_icm_healthy(&icm, 6000, 20000, 3));
}

TEST_F(Icm, FailedReadAfterUntakenSampleDropsIt)
{
    init_ok();
    fake.sample(0, 1, 1, 1, 1, 1, 1);
    oamr_icm_read_sample(&icm, 1000, OAMR_IMU_STAMP_INT1);
    fake.fail = true;
    oamr_icm_read_sample(&icm, 6000, OAMR_IMU_STAMP_INT1);
    oamr_imu_sample_t s{};
    EXPECT_FALSE(oamr_icm_take_sample(&icm, &s));
}

TEST_F(Icm, InvalidCodeIsNeverPublished)
{
    init_ok();
    fake.sample(0, 1, 2, 3, -32768, 5, 6);
    oamr_icm_read_sample(&icm, 1000, OAMR_IMU_STAMP_INT1);
    oamr_imu_sample_t s{};
    EXPECT_FALSE(oamr_icm_take_sample(&icm, &s));
    EXPECT_EQ(icm.counters.invalid_samples, 1u);
    EXPECT_EQ(icm.counters.consecutive_bad, 1u);
    fake.sample(0, 1, 2, 3, 4, 5, 6);
    oamr_icm_read_sample(&icm, 6000, OAMR_IMU_STAMP_INT1);
    EXPECT_TRUE(oamr_icm_take_sample(&icm, &s));
    EXPECT_EQ(icm.counters.consecutive_bad, 0u);
}

TEST_F(Icm, PollReadsOnlyWhenDataReady)
{
    init_ok();
    EXPECT_FALSE(oamr_icm_poll(&icm, 1000));
    fake.sample(0, 1, 2, 3, 4, 5, 6);
    EXPECT_TRUE(oamr_icm_poll(&icm, 2000));
    oamr_imu_sample_t s{};
    ASSERT_TRUE(oamr_icm_take_sample(&icm, &s));
    EXPECT_EQ(s.stamp_source, OAMR_IMU_STAMP_POLL);
}

TEST_F(Icm, NotConfiguredNeverProducesData)
{
    fake.regs[0x75] = 0;
    spi = fake.spi();
    oamr_icm_init(&icm, &spi);
    fake.sample(0, 1, 2, 3, 4, 5, 6);
    oamr_icm_read_sample(&icm, 1000, OAMR_IMU_STAMP_INT1);
    oamr_imu_sample_t s{};
    EXPECT_FALSE(oamr_icm_take_sample(&icm, &s));
}

TEST_F(Icm, HealthNeedsFreshValidSample)
{
    init_ok();
    fake.sample(0, 1, 2, 3, 4, 5, 6);
    oamr_icm_read_sample(&icm, 1000, OAMR_IMU_STAMP_INT1);
    EXPECT_TRUE(oamr_icm_healthy(&icm, 10000, 20000, 3));
    EXPECT_FALSE(oamr_icm_healthy(&icm, 21000, 20000, 3));
}

}  // namespace
