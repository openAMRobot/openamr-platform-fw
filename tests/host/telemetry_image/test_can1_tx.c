/* SPDX-License-Identifier: MIT */
/*
 * Test build of the FDCAN transmit function of the telemetry image
 * (boards/stm32/telemetry_image/src/tlm_can1_tx.c), compiled unchanged
 * against a recording HAL stub. It offers the function an SDO download, NMT
 * commands, an RPDO, SYNC and arbitrary identifiers, and proves that none of
 * them reaches HAL_FDCAN_AddMessageToTxFifoQ. A well-formed SDO
 * initiate-upload request is the positive control: it must reach the HAL.
 *
 * The frames below exist only to be refused. Identifiers and command bytes:
 * CiA 301 (SDO client to server 600h + node, initiate download 2xh, initiate
 * upload 40h; NMT 000h; SYNC 080h; RPDO1 200h + node). Source not verified
 * against the CiA 301 text in this change; they match firmware/canopen
 * (co_cobid.h, co_sdo.h).
 */
#include <string.h>

#include "harness.h"
#include "main.h"
#include "tlm_can1_tx.h"

FDCAN_HandleTypeDef hfdcan1;
static int g_hal_calls;
static FDCAN_TxHeaderTypeDef g_last_header;
static uint8_t g_last_data[8];

HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *hfdcan,
                                                const FDCAN_TxHeaderTypeDef *pTxHeader,
                                                const uint8_t *pTxData)
{
    (void)hfdcan;
    g_hal_calls++;
    g_last_header = *pTxHeader;
    memcpy(g_last_data, pTxData, 8u);
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

static void nothing_but_an_upload_request_reaches_the_hal(void)
{
    tlm_can1_tx_set_started(true);
    const int calls_before = g_hal_calls;
    const tlm_can1_tx_counters_t c0 = tlm_can1_tx_counters();

    /* SDO expedited download (write) of 4 bytes to node 1, index 606Ch:01. */
    const uint8_t dl[8] = {0x23, 0x6C, 0x60, 0x01, 0x11, 0x22, 0x33, 0x44};
    /* Same with the 2-byte and 1-byte and unsized download specifiers. */
    const uint8_t dl2[8] = {0x2B, 0x6C, 0x60, 0x01, 0x11, 0x22, 0x00, 0x00};
    const uint8_t dl1[8] = {0x2F, 0x6C, 0x60, 0x01, 0x11, 0x00, 0x00, 0x00};
    const uint8_t dl0[8] = {0x21, 0x6C, 0x60, 0x01, 0x04, 0x00, 0x00, 0x00};
    /* Upload request bytes, but with a non-zero reserved byte. */
    const uint8_t up_bad[8] = {0x40, 0x6C, 0x60, 0x01, 0x00, 0x00, 0x00, 0x01};
    /* NMT: start node 1, reset node 1, stop all. */
    const uint8_t nmt_start[8] = {0x01, 0x01, 0, 0, 0, 0, 0, 0};
    const uint8_t nmt_reset[8] = {0x81, 0x01, 0, 0, 0, 0, 0, 0};
    const uint8_t nmt_stop[8] = {0x02, 0x00, 0, 0, 0, 0, 0, 0};
    const uint8_t z[8] = {0};
    const uint8_t upload[8] = {0x40, 0x6C, 0x60, 0x01, 0x00, 0x00, 0x00, 0x00};

    const oamr_co_frame_t refused[] = {
        frame(0x601u, false, 8u, dl),        /* SDO download */
        frame(0x601u, false, 8u, dl2),       /* SDO download, 2 bytes */
        frame(0x601u, false, 8u, dl1),       /* SDO download, 1 byte */
        frame(0x601u, false, 8u, dl0),       /* SDO download, size indicated */
        frame(0x601u, false, 8u, up_bad),    /* upload with a reserved byte set */
        frame(0x601u, false, 7u, upload),    /* upload request with DLC 7 */
        frame(0x601u, true, 8u, upload),     /* upload bytes on an extended id */
        frame(0x602u, false, 8u, upload),    /* upload request to another node */
        frame(0x000u, false, 2u, nmt_start), /* NMT start */
        frame(0x000u, false, 2u, nmt_reset), /* NMT reset node */
        frame(0x000u, false, 2u, nmt_stop),  /* NMT stop all */
        frame(0x201u, false, 8u, z),         /* RPDO1 of node 1 */
        frame(0x301u, false, 8u, z),         /* RPDO2 of node 1 */
        frame(0x080u, false, 0u, z),         /* SYNC */
        frame(0x123u, false, 8u, z),         /* arbitrary standard identifier */
        frame(0x7FFu, false, 8u, upload),    /* arbitrary identifier, upload bytes */
        frame(0x18FF0001u, true, 8u, z),     /* arbitrary extended identifier */
        frame(0x701u, false, 1u, z),         /* heartbeat of node 1 (a producer frame) */
    };
    const size_t n = sizeof(refused) / sizeof(refused[0]);
    for (size_t i = 0; i < n; i++)
    {
        CHECK(!tlm_can1_tx(0, &refused[i]));
        CHECK(!tlm_can1_tx_is_upload_request(&refused[i]));
    }
    CHECK(!tlm_can1_tx(0, 0));
    CHECK(g_hal_calls == calls_before); /* none reached HAL_FDCAN_AddMessageToTxFifoQ */
    const tlm_can1_tx_counters_t c1 = tlm_can1_tx_counters();
    CHECK(c1.refused - c0.refused == n + 1u);
    CHECK(c1.sent == c0.sent);

    /* Positive control: the upload request reaches the HAL unchanged. */
    const oamr_co_frame_t ok = frame(0x601u, false, 8u, upload);
    CHECK(tlm_can1_tx(0, &ok));
    CHECK(g_hal_calls == calls_before + 1);
    CHECK(g_last_header.Identifier == 0x601u && g_last_header.IdType == FDCAN_STANDARD_ID &&
          g_last_header.DataLength == FDCAN_DLC_BYTES_8);
    CHECK(memcmp(g_last_data, upload, 8u) == 0);
    CHECK(tlm_can1_tx_counters().sent == c0.sent + 1u);
}

static void nothing_is_sent_before_can1_started(void)
{
    tlm_can1_tx_set_started(false);
    const int calls_before = g_hal_calls;
    const uint8_t upload[8] = {0x40, 0x41, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00};
    const oamr_co_frame_t ok = frame(0x601u, false, 8u, upload);
    const uint32_t failed = tlm_can1_tx_counters().failed;
    CHECK(!tlm_can1_tx(0, &ok));
    CHECK(g_hal_calls == calls_before);
    CHECK(tlm_can1_tx_counters().failed == failed + 1u);
}

int main(void)
{
    RUN(nothing_but_an_upload_request_reaches_the_hal);
    RUN(nothing_is_sent_before_can1_started);
    TEST_END();
}
