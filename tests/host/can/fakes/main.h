/* Narrow STM32 FDCAN/HAL interface for host adapter tests only. */
#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_OK = 0,
    HAL_ERROR = 1,
    HAL_BUSY = 2,
    HAL_TIMEOUT = 3
} HAL_StatusTypeDef;

typedef struct {
    uint32_t reserved;
} FDCAN_GlobalTypeDef;

typedef struct {
    uint32_t FrameFormat;
    uint32_t Mode;
    uint32_t AutoRetransmission;
    uint32_t TransmitPause;
    uint32_t ProtocolException;
    uint32_t NominalPrescaler;
    uint32_t NominalSyncJumpWidth;
    uint32_t NominalTimeSeg1;
    uint32_t NominalTimeSeg2;
    uint32_t DataPrescaler;
    uint32_t DataSyncJumpWidth;
    uint32_t DataTimeSeg1;
    uint32_t DataTimeSeg2;
    uint32_t MessageRAMOffset;
    uint32_t StdFiltersNbr;
    uint32_t ExtFiltersNbr;
    uint32_t RxFifo0ElmtsNbr;
    uint32_t RxFifo0ElmtSize;
    uint32_t RxFifo1ElmtsNbr;
    uint32_t RxFifo1ElmtSize;
    uint32_t RxBuffersNbr;
    uint32_t RxBufferSize;
    uint32_t TxEventsNbr;
    uint32_t TxBuffersNbr;
    uint32_t TxFifoQueueElmtsNbr;
    uint32_t TxFifoQueueMode;
    uint32_t TxElmtSize;
} FDCAN_InitTypeDef;

typedef struct {
    FDCAN_GlobalTypeDef *Instance;
    FDCAN_InitTypeDef Init;
    uint32_t ErrorCode;
} FDCAN_HandleTypeDef;

typedef struct {
    uint32_t IdType;
    uint32_t FilterIndex;
    uint32_t FilterType;
    uint32_t FilterConfig;
    uint32_t FilterID1;
    uint32_t FilterID2;
    uint32_t RxBufferIndex;
    uint32_t IsCalibrationMsg;
} FDCAN_FilterTypeDef;

typedef struct {
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

typedef struct {
    uint32_t Identifier;
    uint32_t IdType;
    uint32_t RxFrameType;
    uint32_t DataLength;
    uint32_t ErrorStateIndicator;
    uint32_t BitRateSwitch;
    uint32_t FDFormat;
    uint32_t RxTimestamp;
    uint32_t FilterIndex;
} FDCAN_RxHeaderTypeDef;

typedef struct {
    uint32_t LastErrorCode;
    uint32_t ErrorPassive;
    uint32_t Warning;
    uint32_t BusOff;
} FDCAN_ProtocolStatusTypeDef;

typedef struct {
    uint32_t TxErrorCnt;
    uint32_t RxErrorCnt;
} FDCAN_ErrorCountersTypeDef;

#define FDCAN1 ((FDCAN_GlobalTypeDef *)(uintptr_t)UINT32_C(0x1001))
#define FDCAN2 ((FDCAN_GlobalTypeDef *)(uintptr_t)UINT32_C(0x1002))
#define FDCAN3 ((FDCAN_GlobalTypeDef *)(uintptr_t)UINT32_C(0x1003))

#define FDCAN_MODE_NORMAL UINT32_C(0)
#define FDCAN_MODE_BUS_MONITORING UINT32_C(2)
#define FDCAN_DATA_BYTES_8 UINT32_C(4)
#define FDCAN_TX_FIFO_OPERATION UINT32_C(0)

#define FDCAN_STANDARD_ID UINT32_C(0)
#define FDCAN_EXTENDED_ID UINT32_C(0x40000000)
#define FDCAN_DATA_FRAME UINT32_C(0)
#define FDCAN_REMOTE_FRAME UINT32_C(0x20000000)
#define FDCAN_CLASSIC_CAN UINT32_C(0)
#define FDCAN_FD_CAN UINT32_C(0x00200000)
#define FDCAN_BRS_OFF UINT32_C(0)
#define FDCAN_BRS_ON UINT32_C(0x00100000)
#define FDCAN_ESI_ACTIVE UINT32_C(0)
#define FDCAN_NO_TX_EVENTS UINT32_C(0)

#define FDCAN_DLC_BYTES_8 UINT32_C(8)
#define FDCAN_DLC_BYTES_12 UINT32_C(9)
#define FDCAN_DLC_BYTES_64 UINT32_C(15)

#define FDCAN_FILTER_RANGE UINT32_C(0)
#define FDCAN_FILTER_MASK UINT32_C(2)
#define FDCAN_FILTER_TO_RXFIFO0 UINT32_C(1)
#define FDCAN_RX_FIFO0 UINT32_C(0x40)
#define FDCAN_REJECT UINT32_C(2)
#define FDCAN_REJECT_REMOTE UINT32_C(1)
#define FDCAN_INTERRUPT_LINE0 UINT32_C(1)

#define FDCAN_IT_RX_FIFO0_NEW_MESSAGE UINT32_C(0x00000001)
#define FDCAN_IT_RX_FIFO0_FULL UINT32_C(0x00000004)
#define FDCAN_IT_RX_FIFO0_MESSAGE_LOST UINT32_C(0x00000008)
#define FDCAN_IT_TX_COMPLETE UINT32_C(0x00000200)
#define FDCAN_IT_TX_ABORT_COMPLETE UINT32_C(0x00000400)
#define FDCAN_IT_RAM_ACCESS_FAILURE UINT32_C(0x00020000)
#define FDCAN_IT_ERROR_LOGGING_OVERFLOW UINT32_C(0x00400000)
#define FDCAN_IT_ERROR_PASSIVE UINT32_C(0x00800000)
#define FDCAN_IT_ERROR_WARNING UINT32_C(0x01000000)
#define FDCAN_IT_BUS_OFF UINT32_C(0x02000000)
#define FDCAN_IT_RAM_WATCHDOG UINT32_C(0x04000000)
#define FDCAN_IT_ARB_PROTOCOL_ERROR UINT32_C(0x08000000)
#define FDCAN_IT_DATA_PROTOCOL_ERROR UINT32_C(0x10000000)
#define FDCAN_IT_RESERVED_ADDRESS_ACCESS UINT32_C(0x20000000)

#define HAL_FDCAN_ERROR_NONE UINT32_C(0)
#define HAL_FDCAN_ERROR_FIFO_FULL UINT32_C(0x00000200)

extern uint32_t fdcan_test_primask;

static inline uint32_t __get_PRIMASK(void)
{
    return fdcan_test_primask;
}

static inline void __disable_irq(void)
{
    fdcan_test_primask = 1U;
}

static inline void __set_PRIMASK(uint32_t primask)
{
    fdcan_test_primask = primask;
}

uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_FDCAN_ConfigFilter(
    FDCAN_HandleTypeDef *hfdcan, const FDCAN_FilterTypeDef *filter);
HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(
    FDCAN_HandleTypeDef *hfdcan, uint32_t non_matching_std,
    uint32_t non_matching_ext, uint32_t reject_remote_std,
    uint32_t reject_remote_ext);
HAL_StatusTypeDef HAL_FDCAN_ConfigInterruptLines(
    FDCAN_HandleTypeDef *hfdcan, uint32_t interrupts, uint32_t line);
HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *hfdcan);
HAL_StatusTypeDef HAL_FDCAN_Stop(FDCAN_HandleTypeDef *hfdcan);
HAL_StatusTypeDef HAL_FDCAN_ActivateNotification(
    FDCAN_HandleTypeDef *hfdcan, uint32_t interrupts, uint32_t buffer_indexes);
HAL_StatusTypeDef HAL_FDCAN_GetProtocolStatus(
    const FDCAN_HandleTypeDef *hfdcan, FDCAN_ProtocolStatusTypeDef *status);
HAL_StatusTypeDef HAL_FDCAN_GetErrorCounters(
    const FDCAN_HandleTypeDef *hfdcan, FDCAN_ErrorCountersTypeDef *counters);
uint32_t HAL_FDCAN_GetRxFifoFillLevel(const FDCAN_HandleTypeDef *hfdcan,
                                      uint32_t rx_fifo);
HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef *hfdcan,
                                         uint32_t rx_location,
                                         FDCAN_RxHeaderTypeDef *header,
                                         uint8_t *data);
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(
    FDCAN_HandleTypeDef *hfdcan, const FDCAN_TxHeaderTypeDef *header,
    const uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_H */
