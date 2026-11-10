// SPDX-License-Identifier: MIT
// Daly BMS adapter: request framing, decoding, fault policy, staleness.
#include <gtest/gtest.h>

#include "fake_can.h"

extern "C" {
#include "oamr/daly_bms.h"
}

namespace {

constexpr uint64_t MS = 1000;

class Daly : public ::testing::Test {
protected:
    void SetUp() override
    {
        oamr_daly_default_config(&cfg);
        tx = can.tx();
        oamr_daly_init(&d, &cfg, &tx);
    }
    void reply(uint8_t id, std::vector<uint8_t> data)
    {
        const oamr_can_frame_t f = make_frame(oamr_daly_response_id(id), data, true);
        EXPECT_TRUE(oamr_daly_on_frame(&d, &f, now));
    }
    void reply_all(std::vector<uint8_t> failure = {0, 0, 0, 0, 0, 0, 0, 0})
    {
        reply(0x90, {0x01, 0x00, 0x01, 0x00, 0x75, 0x3A, 0x03, 0x84});  // 25.6 V, +1.0 A, 90.0 %
        reply(0x91, {0x0C, 0xE4, 0x03, 0x0C, 0xD0, 0x07, 0, 0});        // 3300 mV #3, 3280 mV #7
        reply(0x92, {65, 1, 60, 2, 0, 0, 0, 0});                        // 25 / 20 degC
        reply(0x93, {1, 1, 1, 7, 0, 0, 0x27, 0x10});
        reply(0x98, failure);
    }
    oamr_daly_config_t cfg{};
    oamr_daly_t d{};
    FakeCanTx can;
    oamr_can_tx_t tx{};
    uint64_t now = 1000 * MS;
};

TEST_F(Daly, RequestFrameFormat)
{
    EXPECT_EQ(oamr_daly_request_id(0x90), 0x18900140u);
    EXPECT_EQ(oamr_daly_response_id(0x90), 0x18904001u);
    oamr_daly_poll(&d, now);
    ASSERT_EQ(can.sent.size(), 1u);
    EXPECT_TRUE(can.sent[0].extended);
    EXPECT_EQ(can.sent[0].dlc, 8);
    EXPECT_EQ(can.sent[0].id, 0x18900140u);
    // Rate limited, then cycles through the required IDs.
    oamr_daly_poll(&d, now + 10 * MS);
    EXPECT_EQ(can.sent.size(), 1u);
    std::vector<uint32_t> ids;
    for (int i = 1; i <= 5; i++) {
        oamr_daly_poll(&d, now + static_cast<uint64_t>(i) * cfg.request_period_us);
    }
    for (auto &f : can.sent) ids.push_back((f.id >> 16) & 0xFF);
    EXPECT_EQ(ids, (std::vector<uint32_t>{0x90, 0x91, 0x92, 0x93, 0x98, 0x90}));
}

TEST_F(Daly, DecodesStatus)
{
    reply_all();
    const oamr_bms_status_t *s = oamr_daly_status(&d);
    EXPECT_TRUE(s->have_data);
    EXPECT_NEAR(s->pack_voltage_v, 25.6f, 1e-4);
    EXPECT_NEAR(s->pack_current_a, 1.0f, 1e-4);
    EXPECT_NEAR(s->soc_percent, 90.0f, 1e-4);
    EXPECT_EQ(s->cell_mv_max, 3300);
    EXPECT_EQ(s->cell_mv_min, 3280);
    EXPECT_EQ(s->temp_c_max, 25);
    EXPECT_EQ(s->temp_c_min, 20);
    EXPECT_TRUE(s->charge_mos_on);
    EXPECT_FALSE(s->fault);
    EXPECT_FALSE(s->warning);
    EXPECT_TRUE(oamr_daly_fresh(&d, now));
}

TEST_F(Daly, Level1IsWarningLevel2IsFault)
{
    reply_all({0x01, 0, 0, 0, 0, 0, 0, 0});  // cell voltage high, level 1
    EXPECT_TRUE(oamr_daly_status(&d)->warning);
    EXPECT_FALSE(oamr_daly_status(&d)->fault);
    reply(0x98, {0x02, 0, 0, 0, 0, 0, 0, 0});  // level 2
    EXPECT_TRUE(oamr_daly_status(&d)->fault);
    reply(0x98, {0, 0, 0x08, 0, 0, 0, 0, 0});  // discharge over-current level 2
    EXPECT_TRUE(oamr_daly_status(&d)->fault);
    reply(0x98, {0, 0, 0, 0, 0, 0, 0x04, 0});  // short-circuit protection failure
    EXPECT_TRUE(oamr_daly_status(&d)->fault);
    reply(0x98, {0, 0, 0, 0, 0, 0, 0, 0});
    EXPECT_FALSE(oamr_daly_status(&d)->fault);
}

TEST_F(Daly, IncompleteDataIsNotFresh)
{
    reply(0x90, {0x01, 0x00, 0x01, 0x00, 0x75, 0x30, 0x03, 0x84});
    EXPECT_FALSE(oamr_daly_status(&d)->have_data);
    EXPECT_FALSE(oamr_daly_fresh(&d, now));
}

TEST_F(Daly, StaleAfterTimeout)
{
    reply_all();
    EXPECT_TRUE(oamr_daly_fresh(&d, now + cfg.stale_us - 1));
    EXPECT_FALSE(oamr_daly_fresh(&d, now + cfg.stale_us));
    // One group going quiet is enough to make the set stale.
    now += 2000 * MS;
    reply(0x90, {0, 0, 0, 0, 0x75, 0x30, 0, 0});
    reply(0x91, {0, 0, 0, 0, 0, 0, 0, 0});
    reply(0x92, {40, 0, 40, 0, 0, 0, 0, 0});
    reply(0x93, {0, 0, 0, 0, 0, 0, 0, 0});
    EXPECT_FALSE(oamr_daly_fresh(&d, now + 1500 * MS));  // 0x98 last seen 3.5 s ago
}

TEST_F(Daly, ForeignFramesIgnored)
{
    const oamr_can_frame_t std11 = make_frame(0x701, {0x05});
    EXPECT_FALSE(oamr_daly_on_frame(&d, &std11, now));
    const oamr_can_frame_t other = make_frame(0x18900140u, {0, 0, 0, 0, 0, 0, 0, 0}, true);  // our own request
    EXPECT_FALSE(oamr_daly_on_frame(&d, &other, now));
    const oamr_can_frame_t shortf = make_frame(oamr_daly_response_id(0x90), {1, 2}, true);
    EXPECT_TRUE(oamr_daly_on_frame(&d, &shortf, now));
    EXPECT_EQ(d.frames_ignored, 1u);
    EXPECT_EQ(d.frames_rx, 0u);
}

}  // namespace
