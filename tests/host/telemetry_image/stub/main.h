/* SPDX-License-Identifier: MIT */
/* Host stand-in for the image's main.h: only the FDCAN types and the one HAL
 * call tlm_can1_tx.c uses. HAL_FDCAN_AddMessageToTxFifoQ() is defined by the
 * test and records every frame that reaches it. */
#ifndef TLM_TEST_STUB_MAIN_H
#define TLM_TEST_STUB_MAIN_H

#include <stdint.h>

typedef enum
{
    HAL_OK = 0,
    HAL_ERROR = 1
} HAL_StatusTypeDef;

typedef struct
{
    int unused;
} FDCAN_HandleTypeDef;

typedef struct
{
    uint32_t Identifier;
    uint32_t IdType;
    uint32_t TxFrameType;
    uint32_t DataLength;
    uint32_t ErrorStateIndicator;
    uint32_t BitRateSwitch;
    uint32_t FDFormat;
    uint32_t TxEventFifoControl;
    uint32_t MessageMarker;
} FDCAN_TxHeaderTypeDef;

#define FDCAN_STANDARD_ID 0x00000000u
#define FDCAN_DATA_FRAME 0x00000000u
#define FDCAN_DLC_BYTES_8 0x00000008u
#define FDCAN_ESI_ACTIVE 0x00000000u
#define FDCAN_BRS_OFF 0x00000000u
#define FDCAN_CLASSIC_CAN 0x00000000u
#define FDCAN_NO_TX_EVENTS 0x00000000u

HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *hfdcan,
                                                const FDCAN_TxHeaderTypeDef *pTxHeader,
                                                const uint8_t *pTxData);

#endif
