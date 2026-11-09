// SPDX-License-Identifier: MIT
// ICM-42688-P adapter against a fake SPI register file: identity check,
// 200 Hz configuration with read-back, sample conversion, invalid samples.
#include <gtest/gtest.h>

#include "fake_spi.h"

#include <array>
#include <cmath>
#include <vector>

extern "C" {
#include "oamr/icm42688.h"
}

namespace {

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
