/* SPDX-License-Identifier: MIT */
/*
 * Test build of the CAN2/CAN3 transmit gate of the telemetry image
 * (boards/stm32/telemetry_image/src/tlm_can_aux_tx.c), compiled unchanged and
 * linked with the CAN1 gate and the recording HAL stub of test_can1_tx.c's
 * build. Every frame offered for CAN2 (Daly BMS) or CAN3 (CTL-017) must be
 * refused and counted, and the HAL transmit function must never be called.
 * Positive control: the CAN1 upload request that tlm_can1_tx() accepts does
 * reach the stub, so a call from the aux gate would be seen.
 *
 * Frames (refused only): CiA 301 NMT start 000h [01 10] for node 10h, RPDO1
 * 210h LiftCommand and RPDO2 310h AuxCommand of the CTL-017 dictionary
 * (openamr-upperbody-fw PR #17, section 5), SDO 610h download and upload,
 * heartbeat 701h, a Daly request 18 90 01 40h (daly_rx.h layout with
 * destination and source swapped), a remote-request-sized empty frame.
 */
#include <string.h>

#include "harness.h"
#include "main.h"
#include "tlm_can1_tx.h"
#include "tlm_can_aux_tx.h"

FDCAN_HandleTypeDef hfdcan1, hfdcan2, hfdcan3;
static int g_hal_calls;
static FDCAN_HandleTypeDef *g_last_handle;

HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *hfdcan,
                                                const FDCAN_TxHeaderTypeDef *pTxHeader,
                                                const uint8_t *pTxData)
{
    (void)pTxHeader;
    (void)pTxData;
    g_hal_calls++;
    g_last_handle = hfdcan;
    return HAL_OK;
}

static oamr_co_frame_t frame(uint32_t id, bool ext, uint8_t dlc, const uint8_t d[8])
{
    oamr_co_frame_t f;
    memset(&f, 0, sizeof(f));
    f.id = id;
    f.extended = ext;
    f.dlc = dlc;
    memcpy(f.data, d, 8u);
    return f;
}

static void every_frame_is_refused_on_both_buses(void)
{
    static const uint8_t nmt_start[8] = {0x01, 0x10};
    static const uint8_t lift_cmd[8] = {0x05, 0x01, 0x02};
    static const uint8_t aux_cmd[8] = {0x01, 0x64, 0x01};
    static const uint8_t sdo_dl[8] = {0x2F, 0x17, 0x10, 0x00, 0x64};
    static const uint8_t sdo_ul[8] = {0x40, 0x00, 0x10, 0x00};
    static const uint8_t hb[8] = {0x05};
    static const uint8_t none[8] = {0};
    const oamr_co_frame_t frames[] = {
        frame(0x000u, false, 2u, nmt_start), frame(0x210u, false, 3u, lift_cmd),
        frame(0x310u, false, 3u, aux_cmd),   frame(0x610u, false, 8u, sdo_dl),
        frame(0x610u, false, 8u, sdo_ul),    frame(0x701u, false, 1u, hb),
        frame(0x18900140u, true, 8u, none),  frame(0x601u, false, 8u, sdo_ul),
        frame(0x7FFu, false, 0u, none),
    };
    const unsigned n = (unsigned)(sizeof(frames) / sizeof(frames[0]));
    for (unsigned bus = 0u; bus < (unsigned)TLM_CAN_AUX_N; bus++)
    {
        for (unsigned i = 0u; i < n; i++)
        {
            CHECK(!tlm_can_aux_tx((tlm_can_aux_bus_t)bus, &frames[i]));
        }
        CHECK(tlm_can_aux_tx_refused((tlm_can_aux_bus_t)bus) == n);
    }
    CHECK(!tlm_can_aux_tx(TLM_CAN_AUX_LIFT, 0));
    CHECK(tlm_can_aux_tx_refused(TLM_CAN_AUX_LIFT) == n + 1u);
    CHECK(g_hal_calls == 0);
}

static void an_unknown_bus_index_is_refused_too(void)
{
    static const uint8_t hb[8] = {0x05};
    const oamr_co_frame_t f = frame(0x701u, false, 1u, hb);
    const uint32_t b = tlm_can_aux_tx_refused(TLM_CAN_AUX_BMS);
    const uint32_t l = tlm_can_aux_tx_refused(TLM_CAN_AUX_LIFT);
    CHECK(!tlm_can_aux_tx((tlm_can_aux_bus_t)7, &f));
    CHECK(tlm_can_aux_tx_refused(TLM_CAN_AUX_BMS) == b + 1u);
    CHECK(tlm_can_aux_tx_refused(TLM_CAN_AUX_LIFT) == l + 1u);
    CHECK(tlm_can_aux_tx_refused((tlm_can_aux_bus_t)7) == 0u);
    CHECK(g_hal_calls == 0);
}

static void positive_control_the_stub_sees_a_can1_frame(void)
{
    static const uint8_t ul[8] = {0x40, 0x6C, 0x60, 0x01};
    const oamr_co_frame_t f = frame(0x601u, false, 8u, ul);
    tlm_can1_tx_set_started(true);
    CHECK(tlm_can1_tx(0, &f));
    CHECK(g_hal_calls == 1 && g_last_handle == &hfdcan1);
    /* The same frame on either aux bus: refused, no further HAL call. */
    CHECK(!tlm_can_aux_tx(TLM_CAN_AUX_BMS, &f) && !tlm_can_aux_tx(TLM_CAN_AUX_LIFT, &f));
    CHECK(g_hal_calls == 1);
}

int main(void)
{
    RUN(every_frame_is_refused_on_both_buses);
    RUN(an_unknown_bus_index_is_refused_too);
    RUN(positive_control_the_stub_sees_a_can1_frame);
    TEST_END();
}
