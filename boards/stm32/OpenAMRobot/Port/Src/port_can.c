/* SPDX-License-Identifier: MIT */
/*
 * FDCAN glue: filters, start, non-blocking transmit, polled receive and
 * bus-off detection and restart. Called from the FDCANx_Init USER CODE block
 * and from the control tick.
 *
 * Filters: the .ioc reserves one standard filter and no extended filter per
 * instance. CAN1 (ZLAC8015D) and CAN3 use 11-bit CANopen identifiers: the
 * standard filter accepts all of them into RX FIFO 0 and extended frames are
 * rejected. CAN2 (Daly BMS) uses 29-bit identifiers: standard frames are
 * rejected and non-matching extended frames are accepted into RX FIFO 0
 * through the global filter.
 */
#include "main.h"
#include "port_board.h"
#include "port_internal.h"

#include <string.h>

void port_fdcan_start(FDCAN_HandleTypeDef *h, bool extended_ids)
{
    if (!extended_ids) {
        FDCAN_FilterTypeDef f = {0};
        f.IdType = FDCAN_STANDARD_ID;
        f.FilterIndex = 0;
        f.FilterType = FDCAN_FILTER_RANGE;
        f.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
        f.FilterID1 = 0x000u;
        f.FilterID2 = 0x7FFu;
        if (HAL_FDCAN_ConfigFilter(h, &f) != HAL_OK) {
            Error_Handler();
        }
        if (HAL_FDCAN_ConfigGlobalFilter(h, FDCAN_REJECT, FDCAN_REJECT, FDCAN_REJECT_REMOTE,
                                         FDCAN_REJECT_REMOTE) != HAL_OK) {
            Error_Handler();
        }
    } else {
        if (HAL_FDCAN_ConfigGlobalFilter(h, FDCAN_REJECT, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_REJECT_REMOTE,
                                         FDCAN_REJECT_REMOTE) != HAL_OK) {
            Error_Handler();
        }
    }
    if (HAL_FDCAN_Start(h) != HAL_OK) {
        Error_Handler();
    }
}

static const uint32_t k_dlc[9] = {
    FDCAN_DLC_BYTES_0, FDCAN_DLC_BYTES_1, FDCAN_DLC_BYTES_2, FDCAN_DLC_BYTES_3, FDCAN_DLC_BYTES_4,
    FDCAN_DLC_BYTES_5, FDCAN_DLC_BYTES_6, FDCAN_DLC_BYTES_7, FDCAN_DLC_BYTES_8,
};

static bool can_send(void *ctx, const oamr_can_frame_t *frame)
{
    FDCAN_HandleTypeDef *h = (FDCAN_HandleTypeDef *)ctx;
    if (frame->dlc > 8u || HAL_FDCAN_GetTxFifoFreeLevel(h) == 0u || port_can_bus_off(h)) {
        return false;
    }
    FDCAN_TxHeaderTypeDef hdr = {0};
    hdr.Identifier = frame->id;
    hdr.IdType = frame->extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    hdr.TxFrameType = FDCAN_DATA_FRAME;
    hdr.DataLength = k_dlc[frame->dlc];
    hdr.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    hdr.BitRateSwitch = FDCAN_BRS_OFF;
    hdr.FDFormat = FDCAN_CLASSIC_CAN;
    hdr.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    hdr.MessageMarker = 0;
    return HAL_FDCAN_AddMessageToTxFifoQ(h, &hdr, frame->data) == HAL_OK;
}

oamr_can_tx_t port_can_tx(FDCAN_HandleTypeDef *h)
{
    oamr_can_tx_t tx = {can_send, h};
    return tx;
}

bool port_can_receive(FDCAN_HandleTypeDef *h, oamr_can_frame_t *f)
{
    if (HAL_FDCAN_GetRxFifoFillLevel(h, FDCAN_RX_FIFO0) == 0u) {
        return false;
    }
    FDCAN_RxHeaderTypeDef hdr;
    uint8_t data[8];
    if (HAL_FDCAN_GetRxMessage(h, FDCAN_RX_FIFO0, &hdr, data) != HAL_OK) {
        return false;
    }
    memset(f, 0, sizeof(*f));
    f->id = hdr.Identifier;
    f->extended = (hdr.IdType == FDCAN_EXTENDED_ID);
    /* Classic frames: the DLC code equals the byte count for 0 to 8. */
    uint32_t n = hdr.DataLength;
    if (n > 8u) {
        n = 8u;
    }
    f->dlc = (uint8_t)n;
    memcpy(f->data, data, n);
    return true;
}

bool port_can_bus_off(FDCAN_HandleTypeDef *h)
{
    return (h->Instance->PSR & FDCAN_PSR_BO) != 0u;
}

/* After bus-off the FDCAN sets CCCR.INIT. Stopping and starting again clears
 * INIT and lets the controller run the bus-off recovery sequence (128 x 11
 * recessive bits) before it takes part in traffic. */
void port_can_restart(FDCAN_HandleTypeDef *h)
{
    (void)HAL_FDCAN_Stop(h);
    (void)HAL_FDCAN_Start(h);
}
