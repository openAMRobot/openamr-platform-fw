#include "main.h"
#include "fdcan_adapter.h"

#include "oamr/fdcan_recovery.h"

#include <string.h>

extern FDCAN_HandleTypeDef hfdcan1;
extern FDCAN_HandleTypeDef hfdcan2;
extern FDCAN_HandleTypeDef hfdcan3;

#define FDCAN_ADAPTER_RX_MASK                                                            \
  (FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_FULL |                              \
   FDCAN_IT_RX_FIFO0_MESSAGE_LOST)
#define FDCAN_ADAPTER_ERROR_MASK                                                         \
  (FDCAN_IT_ERROR_WARNING | FDCAN_IT_ERROR_PASSIVE | FDCAN_IT_BUS_OFF |                 \
   FDCAN_IT_ERROR_LOGGING_OVERFLOW | FDCAN_IT_RAM_ACCESS_FAILURE |                      \
   FDCAN_IT_RAM_WATCHDOG | FDCAN_IT_ARB_PROTOCOL_ERROR |                                \
   FDCAN_IT_DATA_PROTOCOL_ERROR | FDCAN_IT_RESERVED_ADDRESS_ACCESS)
#define FDCAN_ADAPTER_NOTIFICATION_MASK (FDCAN_ADAPTER_RX_MASK | FDCAN_ADAPTER_ERROR_MASK)
#define FDCAN_ADAPTER_TX_MASK (FDCAN_IT_TX_COMPLETE | FDCAN_IT_TX_ABORT_COMPLETE)
#define FDCAN_ADAPTER_TX_BUFFERS UINT32_C(0xff)

typedef struct
{
  FDCAN_HandleTypeDef *handle;
  FDCAN_GlobalTypeDef *instance;
  oamr_bounded_queue_t queue;
  oamr_can_frame_t queue_storage[FDCAN_ADAPTER_RX_QUEUE_CAPACITY];
  volatile fdcan_adapter_diagnostics_t diagnostics;
  oamr_fdcan_recovery_t recovery;
  oamr_fdcan_recovery_event_t recovery_events[4];
  bool recovery_initialized;
} fdcan_adapter_state_t;

static fdcan_adapter_state_t buses[FDCAN_ADAPTER_BUS_COUNT] =
{
  { .handle = &hfdcan1, .instance = FDCAN1 },
  { .handle = &hfdcan2, .instance = FDCAN2 },
  { .handle = &hfdcan3, .instance = FDCAN3 }
};

static void fdcan_adapter_increment(volatile uint32_t *counter)
{
  if (*counter < UINT32_MAX)
  {
    ++(*counter);
  }
}

static fdcan_adapter_state_t *fdcan_adapter_from_handle(const FDCAN_HandleTypeDef *hfdcan)
{
  size_t i;

  if (hfdcan == NULL)
  {
    return NULL;
  }
  for (i = 0U; i < (size_t)FDCAN_ADAPTER_BUS_COUNT; ++i)
  {
    if (hfdcan == buses[i].handle)
    {
      return &buses[i];
    }
  }
  return NULL;
}

static uint32_t fdcan_adapter_dlc_to_bytes(uint32_t dlc)
{
  static const uint8_t lengths[16] =
  {
    0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U,
    8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U
  };

  return dlc <= FDCAN_DLC_BYTES_64 ? lengths[dlc] : UINT32_MAX;
}

static uint64_t fdcan_adapter_time_us(void)
{
  static uint64_t last_us;
  uint32_t now_ms = HAL_GetTick();
  uint64_t now_us = (uint64_t)now_ms * UINT64_C(1000);

  /* Preserve ordering if multiple frames arrive in one 1 ms HAL tick. */
  if (now_us <= last_us)
  {
    if (last_us != UINT64_MAX)
    {
      now_us = last_us + 1U;
    }
  }
  last_us = now_us;
  return now_us;
}

static bool fdcan_adapter_start_bus(fdcan_adapter_state_t *state)
{
  if (!state->recovery_initialized)
  {
    state->recovery_initialized =
        oamr_fdcan_recovery_init(&state->recovery, state->recovery_events,
                                 sizeof(state->recovery_events), 4U) == OAMR_STATUS_OK;
    if (!state->recovery_initialized)
    {
      return false;
    }
  }
  FDCAN_FilterTypeDef filter = {0};
  bool is_daly = state->instance == FDCAN2;
  HAL_StatusTypeDef status;

  filter.FilterIndex = 0U;
  filter.FilterType = is_daly ? FDCAN_FILTER_MASK : FDCAN_FILTER_RANGE;
  filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
  filter.FilterID1 = is_daly ? FDCAN_ADAPTER_DALY_RX_ID : 0U;
  filter.FilterID2 = is_daly ? OAMR_CAN_EXTENDED_ID_MAX : OAMR_CAN_STANDARD_ID_MAX;
  if (is_daly)
  {
    filter.IdType = FDCAN_EXTENDED_ID;
    status = HAL_FDCAN_ConfigFilter(state->handle, &filter);
  }
  else
  {
    filter.IdType = FDCAN_STANDARD_ID;
    status = HAL_FDCAN_ConfigFilter(state->handle, &filter);
  }
  if (status != HAL_OK)
  {
    return false;
  }

  /* FDCAN2 rejects every unmatched standard/extended ID. Diagnostic-only
   * FDCAN1/3 use a broad standard range and reject every extended ID. No
   * non-matching frames are accepted. Remote frames are always rejected. */
  status = HAL_FDCAN_ConfigGlobalFilter(state->handle, FDCAN_REJECT,
                                        FDCAN_REJECT, FDCAN_REJECT_REMOTE,
                                        FDCAN_REJECT_REMOTE);
  if (status != HAL_OK)
  {
    return false;
  }
  status = HAL_FDCAN_ConfigInterruptLines(state->handle,
                                          FDCAN_ADAPTER_NOTIFICATION_MASK |
                                            (is_daly ? FDCAN_ADAPTER_TX_MASK : 0U),
                                          FDCAN_INTERRUPT_LINE0);
  if (status != HAL_OK)
  {
    return false;
  }
  if (HAL_FDCAN_Start(state->handle) != HAL_OK)
  {
    return false;
  }
  status = HAL_FDCAN_ActivateNotification(state->handle,
                                          FDCAN_ADAPTER_NOTIFICATION_MASK, 0U);
  if ((status == HAL_OK) && is_daly)
  {
    status = HAL_FDCAN_ActivateNotification(state->handle, FDCAN_ADAPTER_TX_MASK,
                                            FDCAN_ADAPTER_TX_BUFFERS);
  }
  if (status != HAL_OK)
  {
    (void)HAL_FDCAN_Stop(state->handle);
    return false;
  }
  state->diagnostics.started = true;
  return true;
}

bool fdcan_adapter_init(void)
{
  bool all_started = true;
  size_t i;

  for (i = 0U; i < (size_t)FDCAN_ADAPTER_BUS_COUNT; ++i)
  {
    fdcan_adapter_state_t *state = &buses[i];
    FDCAN_ProtocolStatusTypeDef protocol = {0};
    FDCAN_ErrorCountersTypeDef counters = {0};

    state->diagnostics = (fdcan_adapter_diagnostics_t){0};
    state->recovery_initialized = false;
    memset(&state->recovery, 0, sizeof(state->recovery));
    if ((state->handle == NULL) || (state->handle->Instance != state->instance) ||
        (oamr_bounded_queue_init(&state->queue, state->queue_storage,
                                 sizeof(state->queue_storage), sizeof(oamr_can_frame_t),
                                 FDCAN_ADAPTER_RX_QUEUE_CAPACITY) != OAMR_STATUS_OK))
    {
      fdcan_adapter_increment(&state->diagnostics.start_errors);
      all_started = false;
      continue;
    }
    if ((HAL_FDCAN_GetProtocolStatus(state->handle, &protocol) == HAL_OK) &&
        (protocol.BusOff != 0U))
    {
      state->diagnostics.bus_off = true;
    }
    if (HAL_FDCAN_GetErrorCounters(state->handle, &counters) == HAL_OK)
    {
      state->diagnostics.tx_error_count = counters.TxErrorCnt;
      state->diagnostics.rx_error_count = counters.RxErrorCnt;
    }
    if (!fdcan_adapter_start_bus(state))
    {
      fdcan_adapter_increment(&state->diagnostics.start_errors);
      state->diagnostics.last_hal_error = state->handle->ErrorCode;
      all_started = false;
    }
  }
  return all_started;
}

void fdcan_adapter_poll(uint32_t now_ms)
{
  size_t i;


  for (i = 0U; i < (size_t)FDCAN_ADAPTER_BUS_COUNT; ++i)
  {
    fdcan_adapter_state_t *state = &buses[i];
    FDCAN_ProtocolStatusTypeDef protocol = {0};
    FDCAN_ErrorCountersTypeDef counters = {0};

    if (!state->diagnostics.started)
    {
      continue;
    }
    if (HAL_FDCAN_GetProtocolStatus(state->handle, &protocol) == HAL_OK)
    {
      state->diagnostics.warning = protocol.Warning != 0U;
      state->diagnostics.passive = protocol.ErrorPassive != 0U;
      state->diagnostics.bus_off = protocol.BusOff != 0U;
      state->diagnostics.last_protocol_error = protocol.LastErrorCode;
    }
    else
    {
      state->diagnostics.last_hal_error = state->handle->ErrorCode;
    }
    if (HAL_FDCAN_GetErrorCounters(state->handle, &counters) == HAL_OK)
    {
      state->diagnostics.tx_error_count = counters.TxErrorCnt;
      state->diagnostics.rx_error_count = counters.RxErrorCnt;
    }
    else
    {
      state->diagnostics.last_hal_error = state->handle->ErrorCode;
    }
    if (state->recovery_initialized)
    {
      oamr_fdcan_recovery_attempt_request_t request;
      uint64_t now_us = (uint64_t)now_ms * UINT64_C(1000);

      (void)oamr_fdcan_recovery_record_error_status(
          &state->recovery, now_us, state->diagnostics.warning,
          state->diagnostics.passive, state->diagnostics.bus_off,
          (uint8_t)state->diagnostics.tx_error_count, (uint8_t)state->diagnostics.rx_error_count);
      if (oamr_fdcan_recovery_poll(&state->recovery, now_us, &request) == OAMR_STATUS_OK &&
          request.required)
      {
        oamr_status_t result = OAMR_STATUS_ERROR;
        FDCAN_ProtocolStatusTypeDef recovered_protocol = {0};

        if (HAL_FDCAN_Stop(state->handle) == HAL_OK && fdcan_adapter_start_bus(state) &&
            HAL_FDCAN_GetProtocolStatus(state->handle, &recovered_protocol) == HAL_OK &&
            recovered_protocol.BusOff == 0U)
        {
          result = OAMR_STATUS_OK;
        }

        (void)oamr_fdcan_recovery_complete_attempt(&state->recovery, now_us, result);
      }
    }
  }
}

oamr_status_t fdcan_adapter_receive(fdcan_adapter_bus_t bus,
                                    oamr_can_frame_t *frame)
{
  uint32_t primask;
  oamr_status_t status;

  if ((bus >= FDCAN_ADAPTER_BUS_COUNT) || (frame == NULL))
  {
    return OAMR_STATUS_INVALID;
  }
  /* Queue push/pop is single-producer/single-consumer; serialize the short
   * index update against the ISR without disabling other work for decoding. */
  primask = __get_PRIMASK();
  __disable_irq();
  status = oamr_bounded_queue_pop(&buses[bus].queue, frame);
  __set_PRIMASK(primask);
  return status;
}

oamr_status_t fdcan_adapter_send_daly_request(const uint8_t data[FDCAN_ADAPTER_DALY_DLC])
{
  FDCAN_TxHeaderTypeDef header = {0};
  fdcan_adapter_state_t *state = &buses[FDCAN_ADAPTER_FDCAN2];
  HAL_StatusTypeDef status;

  if ((data == NULL) || !state->diagnostics.started || !state->recovery_initialized ||
      oamr_fdcan_recovery_begin_normal_tx(&state->recovery) != OAMR_STATUS_OK)
  {
    fdcan_adapter_increment(&state->diagnostics.tx_rejected);
    return OAMR_STATUS_INVALID;
  }
  header.Identifier = FDCAN_ADAPTER_DALY_TX_ID;
  header.IdType = FDCAN_EXTENDED_ID;
  header.TxFrameType = FDCAN_DATA_FRAME;
  header.DataLength = FDCAN_DLC_BYTES_8;
  header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  header.BitRateSwitch = FDCAN_BRS_OFF;
  header.FDFormat = FDCAN_CLASSIC_CAN;
  header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  status = HAL_FDCAN_AddMessageToTxFifoQ(state->handle, &header, data);
  if (status == HAL_BUSY)
  {
    return OAMR_STATUS_TIMEOUT;
  }
  if (status != HAL_OK)
  {
    fdcan_adapter_increment(&state->diagnostics.tx_hal_errors);
    state->diagnostics.last_hal_error = state->handle->ErrorCode;
    return state->handle->ErrorCode == HAL_FDCAN_ERROR_FIFO_FULL ?
             OAMR_STATUS_OVERFLOW : OAMR_STATUS_ERROR;
  }
  fdcan_adapter_increment(&state->diagnostics.tx_submitted);
  return OAMR_STATUS_OK;
}

bool fdcan_adapter_diagnostics(fdcan_adapter_bus_t bus,
                               fdcan_adapter_diagnostics_t *diagnostics)
{
  uint32_t primask;

  if ((bus >= FDCAN_ADAPTER_BUS_COUNT) || (diagnostics == NULL))
  {
    return false;
  }
  primask = __get_PRIMASK();
  __disable_irq();
  *diagnostics = buses[bus].diagnostics;
  __set_PRIMASK(primask);
  return true;
}

void fdcan_adapter_rx_fifo0(FDCAN_HandleTypeDef *hfdcan, uint32_t interrupts)
{
  fdcan_adapter_state_t *state = fdcan_adapter_from_handle(hfdcan);
  size_t count = 0U;

  if ((state == NULL) || !state->diagnostics.started)
  {
    return;
  }
  fdcan_adapter_increment(&state->diagnostics.rx_events);
  if ((interrupts & (FDCAN_IT_RX_FIFO0_FULL | FDCAN_IT_RX_FIFO0_MESSAGE_LOST |
                     FDCAN_IT_RX_FIFO0_NEW_MESSAGE)) == 0U)
  {
    return;
  }
  if ((interrupts & (FDCAN_IT_RX_FIFO0_FULL | FDCAN_IT_RX_FIFO0_MESSAGE_LOST)) != 0U)
  {
    fdcan_adapter_increment(&state->diagnostics.rx_fifo_overflows);
  }

  /* Drain on full/lost as well as new-message events. Bound HAL work per
   * callback; another pending RX interrupt drains the rest. */
  while ((count < FDCAN_ADAPTER_POLL_LIMIT) &&
         (HAL_FDCAN_GetRxFifoFillLevel(state->handle, FDCAN_RX_FIFO0) > 0U))
  {
    FDCAN_RxHeaderTypeDef header = {0};
    oamr_can_frame_t frame = {0};
    uint32_t length;

    if (HAL_FDCAN_GetRxMessage(state->handle, FDCAN_RX_FIFO0, &header,
                               frame.data) != HAL_OK)
    {
      fdcan_adapter_increment(&state->diagnostics.rx_hal_errors);
      state->diagnostics.last_hal_error = state->handle->ErrorCode;
      break;
    }
    length = fdcan_adapter_dlc_to_bytes(header.DataLength);
    if ((length > OAMR_CAN_MAX_DLC) || (header.FDFormat != FDCAN_CLASSIC_CAN) ||
        (header.RxFrameType != FDCAN_DATA_FRAME) ||
        ((state->instance == FDCAN2) &&
         ((header.IdType != FDCAN_EXTENDED_ID) ||
          (header.Identifier != FDCAN_ADAPTER_DALY_RX_ID) ||
          (length != FDCAN_ADAPTER_DALY_DLC))) ||
        ((state->instance != FDCAN2) && (header.IdType != FDCAN_STANDARD_ID)))
    {
      fdcan_adapter_increment(&state->diagnostics.rx_malformed_frames);
      ++count;
      continue;
    }
    frame.timestamp_us = fdcan_adapter_time_us();
    frame.id = header.Identifier;
    frame.id_type = header.IdType == FDCAN_EXTENDED_ID ?
                      OAMR_CAN_ID_EXTENDED : OAMR_CAN_ID_STANDARD;
    frame.dlc = (uint8_t)length;
    if (oamr_bounded_queue_push(&state->queue, &frame) == OAMR_STATUS_OVERFLOW)
    {
      fdcan_adapter_increment(&state->diagnostics.rx_queue_overflows);
    }
    else
    {
      fdcan_adapter_increment(&state->diagnostics.rx_frames);
    }
    ++count;
  }
}

void fdcan_adapter_tx_complete(FDCAN_HandleTypeDef *hfdcan, uint32_t buffer_indexes)
{
  fdcan_adapter_state_t *state = fdcan_adapter_from_handle(hfdcan);

  if ((state != NULL) && (state->instance == FDCAN2) && (buffer_indexes != 0U))
  {
    fdcan_adapter_increment(&state->diagnostics.tx_complete);
  }
}

void fdcan_adapter_tx_aborted(FDCAN_HandleTypeDef *hfdcan, uint32_t buffer_indexes)
{
  fdcan_adapter_state_t *state = fdcan_adapter_from_handle(hfdcan);

  if ((state != NULL) && (state->instance == FDCAN2) && (buffer_indexes != 0U))
  {
    fdcan_adapter_increment(&state->diagnostics.tx_aborted);
  }
}

void fdcan_adapter_error(FDCAN_HandleTypeDef *hfdcan)
{
  fdcan_adapter_state_t *state = fdcan_adapter_from_handle(hfdcan);

  if (state != NULL)
  {
    fdcan_adapter_increment(&state->diagnostics.error_events);
    state->diagnostics.last_hal_error = state->handle->ErrorCode;
  }
}

void fdcan_adapter_error_status(FDCAN_HandleTypeDef *hfdcan, uint32_t interrupts)
{
  fdcan_adapter_state_t *state = fdcan_adapter_from_handle(hfdcan);
  FDCAN_ProtocolStatusTypeDef protocol = {0};

  if (state == NULL)
  {
    return;
  }
  if ((interrupts & FDCAN_IT_ERROR_WARNING) != 0U)
  {
    fdcan_adapter_increment(&state->diagnostics.warning_events);
  }
  if ((interrupts & FDCAN_IT_ERROR_PASSIVE) != 0U)
  {
    fdcan_adapter_increment(&state->diagnostics.passive_events);
  }
  if ((interrupts & FDCAN_IT_BUS_OFF) != 0U)
  {
    fdcan_adapter_increment(&state->diagnostics.bus_off_events);
  }
  if (HAL_FDCAN_GetProtocolStatus(state->handle, &protocol) == HAL_OK)
  {
    state->diagnostics.warning = protocol.Warning != 0U;
    state->diagnostics.passive = protocol.ErrorPassive != 0U;
    state->diagnostics.bus_off = protocol.BusOff != 0U;
    state->diagnostics.last_protocol_error = protocol.LastErrorCode;
  }
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t interrupts)
{
  fdcan_adapter_rx_fifo0(hfdcan, interrupts);
}

void HAL_FDCAN_TxBufferCompleteCallback(FDCAN_HandleTypeDef *hfdcan,
                                        uint32_t buffer_indexes)
{
  fdcan_adapter_tx_complete(hfdcan, buffer_indexes);
}

void HAL_FDCAN_TxBufferAbortCallback(FDCAN_HandleTypeDef *hfdcan,
                                     uint32_t buffer_indexes)
{
  fdcan_adapter_tx_aborted(hfdcan, buffer_indexes);
}

void HAL_FDCAN_ErrorCallback(FDCAN_HandleTypeDef *hfdcan)
{
  fdcan_adapter_error(hfdcan);
}

void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan, uint32_t interrupts)
{
  fdcan_adapter_error_status(hfdcan, interrupts);
}
