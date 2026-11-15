/* SPDX-License-Identifier: MIT */
#include "tlm_can1_tx.h"

#include "main.h"

#include "oamr/co_cobid.h"
#include "oamr/co_sdo.h"
#include "tlm_params.h"

extern FDCAN_HandleTypeDef hfdcan1; /* generated main.c, or the test stub */

static bool s_started;
static tlm_can1_tx_counters_t s_cnt;

bool tlm_can1_tx_is_upload_request(const oamr_co_frame_t *f)
{
    return f != 0 && !f->extended && f->id == OAMR_CO_COB_SDO_RX_BASE + TLM_DRIVE_NODE_ID &&
           f->dlc == 8u && f->data[0] == OAMR_CO_SDO_CCS_UPLOAD_INIT && f->data[4] == 0u &&
           f->data[5] == 0u && f->data[6] == 0u && f->data[7] == 0u;
}

void tlm_can1_tx_set_started(bool started)
{
    s_started = started;
}

bool tlm_can1_tx(void *ctx, const oamr_co_frame_t *f)
{
    (void)ctx;
    if (!tlm_can1_tx_is_upload_request(f))
    {
        s_cnt.refused++;
        return false;
    }
    if (!s_started)
    {
        s_cnt.failed++;
        return false;
    }
    FDCAN_TxHeaderTypeDef h = {0};
    h.Identifier = f->id;
    h.IdType = FDCAN_STANDARD_ID;
    h.TxFrameType = FDCAN_DATA_FRAME;
    h.DataLength = FDCAN_DLC_BYTES_8;
    h.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    h.BitRateSwitch = FDCAN_BRS_OFF;
    h.FDFormat = FDCAN_CLASSIC_CAN;
    h.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &h, f->data) != HAL_OK)
    {
        s_cnt.failed++;
        return false;
    }
    s_cnt.sent++;
    return true;
}

tlm_can1_tx_counters_t tlm_can1_tx_counters(void)
{
    return s_cnt;
}
