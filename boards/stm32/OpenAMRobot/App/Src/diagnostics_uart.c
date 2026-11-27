#include "diagnostics_uart.h"

#include "oamr/reset_cause.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
  UART_HandleTypeDef *uart;
  uint32_t reset_flags;
  uint8_t buffer[DIAGNOSTICS_UART_TX_CAPACITY];
  volatile uint16_t read_index;
  volatile uint16_t write_index;
  volatile bool active;
  volatile bool ready;
  volatile uint32_t dropped_bytes;
  volatile uint32_t tx_errors;
} diagnostics_uart_state_t;

static diagnostics_uart_state_t diagnostics;

static void diagnostics_increment(volatile uint32_t *counter)
{
  if (*counter < UINT32_MAX)
  {
    ++(*counter);
  }
}

/* Called only while TX is stopped, before read_index is made visible again. */
static void diagnostics_send_next(void)
{
  uint16_t read_index = diagnostics.read_index;
  uint16_t write_index = diagnostics.write_index;
  uint16_t length;
  HAL_StatusTypeDef status;

  if (!diagnostics.ready || (diagnostics.uart == NULL))
  {
    return;
  }
  if (read_index == write_index)
  {
    diagnostics.active = false;
    return;
  }
  length = (read_index <= write_index) ? (uint16_t)(write_index - read_index) :
           (uint16_t)(DIAGNOSTICS_UART_TX_CAPACITY - read_index);
  status = HAL_UART_Transmit_IT(diagnostics.uart, &diagnostics.buffer[read_index], length);
  if (status == HAL_BUSY)
  {
    /* An owner outside this adapter is using the UART. Leave queued bytes in
     * place; the main loop retries without waiting. */
    diagnostics.active = false;
  }
  else if (status == HAL_OK)
  {
    diagnostics.active = true;
  }
  else
  {
    diagnostics.active = false;
    diagnostics_increment(&diagnostics.tx_errors);
    diagnostics.read_index = (uint16_t)((read_index + length) % DIAGNOSTICS_UART_TX_CAPACITY);
  }
}

uint32_t diagnostics_uart_capture_reset_flags(void)
{
  /* RSR is read before RMVF: software reset, pin reset, watchdog reset, and
   * low-power/BOR causes remain distinguishable in the boot report. */
  uint32_t flags = RCC->RSR;
  __HAL_RCC_CLEAR_RESET_FLAGS();
  return flags;
}

bool diagnostics_uart_init(UART_HandleTypeDef *huart, uint32_t reset_flags)
{
  if ((huart == NULL) || (huart->Instance != USART3))
  {
    return false;
  }
  memset(&diagnostics, 0, sizeof(diagnostics));
  diagnostics.uart = huart;
  diagnostics.reset_flags = reset_flags;
  diagnostics.ready = true;
  return true;
}

bool diagnostics_uart_write_bytes(const void *data, size_t size)
{
  uint32_t primask;
  uint16_t read_index;
  uint16_t write_index;
  uint16_t free_bytes;
  size_t first_length;

  if (!diagnostics.ready || (data == NULL) || (size == 0U) ||
      (size >= DIAGNOSTICS_UART_TX_CAPACITY))
  {
    return false;
  }

  primask = __get_PRIMASK();
  __disable_irq();
  read_index = diagnostics.read_index;
  write_index = diagnostics.write_index;
  free_bytes = (read_index <= write_index) ?
               (uint16_t)(DIAGNOSTICS_UART_TX_CAPACITY - (uint32_t)(write_index - read_index) - 1U) :
               (uint16_t)(read_index - write_index - 1U);
  if ((size_t)free_bytes < size)
  {
    /* Reject the entire record: queued bytes and the active HAL buffer stay
     * immutable until the completion interrupt retires them. */
    diagnostics.dropped_bytes = (diagnostics.dropped_bytes > UINT32_MAX - size) ?
                                UINT32_MAX : diagnostics.dropped_bytes + (uint32_t)size;
    __set_PRIMASK(primask);
    return false;
  }
  first_length = DIAGNOSTICS_UART_TX_CAPACITY - write_index;
  if (first_length > size)
  {
    first_length = size;
  }
  memcpy(&diagnostics.buffer[write_index], data, first_length);
  if (size > first_length)
  {
    memcpy(&diagnostics.buffer[0], (const uint8_t *)data + first_length,
           size - first_length);
  }
  diagnostics.write_index = (uint16_t)((write_index + size) % DIAGNOSTICS_UART_TX_CAPACITY);
  if (!diagnostics.active)
  {
    diagnostics.active = true;
    diagnostics_send_next();
  }
  __set_PRIMASK(primask);
  return true;
}

oamr_transport_result_t diagnostics_uart_transport_write(
    void *context, const uint8_t *data, size_t data_size, size_t *transferred)
{
  (void)context;

  if (transferred == NULL)
  {
    return OAMR_TRANSPORT_INVALID;
  }
  *transferred = 0U;
  if ((data == NULL) || (data_size == 0U) || (data_size >= DIAGNOSTICS_UART_TX_CAPACITY))
  {
    return OAMR_TRANSPORT_INVALID;
  }
  if (!diagnostics_uart_write_bytes(data, data_size))
  {
    return OAMR_TRANSPORT_WOULD_BLOCK;
  }
  *transferred = data_size;
  return OAMR_TRANSPORT_OK;
}

bool diagnostics_uart_write(const char *text)
{
  size_t length;
  bool accepted = true;

  if ((text == NULL) || !diagnostics.ready)
  {
    return false;
  }
  length = strnlen(text, DIAGNOSTICS_UART_MESSAGE_MAX + 1U);
  if (length > DIAGNOSTICS_UART_MESSAGE_MAX)
  {
    length = DIAGNOSTICS_UART_MESSAGE_MAX;
    accepted = false;
  }
  return diagnostics_uart_write_bytes(text, length) && accepted;
}

void diagnostics_uart_poll(void)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  if (diagnostics.ready && !diagnostics.active && (diagnostics.read_index != diagnostics.write_index) &&
      (diagnostics.uart->gState == HAL_UART_STATE_READY))
  {
    diagnostics.active = true;
    diagnostics_send_next();
  }
  __set_PRIMASK(primask);
}

void diagnostics_uart_report_boot(bool sensors_ready, bool fdcan_ready)
{
  static const oamr_reset_flag_definitions_t definitions = {
    .power_on = RCC_RSR_PORRSTF,
    .pin = RCC_RSR_PINRSTF,
    .software = RCC_RSR_SFTRSTF,
    .iwdg = RCC_RSR_IWDG1RSTF,
    .wwdg = RCC_RSR_WWDG1RSTF,
    .low_power = RCC_RSR_LPWRRSTF,
    .bor = RCC_RSR_BORRSTF,
    .other = RCC_RSR_CPURSTF | RCC_RSR_D1RSTF | RCC_RSR_D2RSTF
  };
  oamr_reset_cause_t cause;
  char message[DIAGNOSTICS_UART_MESSAGE_MAX + 1U];

  if (oamr_reset_cause_decode(diagnostics.reset_flags, &definitions, &cause) == OAMR_STATUS_OK)
  {
    (void)snprintf(message, sizeof(message),
                   "\r\nOAMR reset raw=0x%08lX causes=0x%02lX unknown=0x%08lX\r\n",
                   (unsigned long)cause.raw_flags, (unsigned long)cause.causes,
                   (unsigned long)cause.unknown_flags);
    (void)diagnostics_uart_write(message);
  }
  (void)snprintf(message, sizeof(message), "OAMR init sensors=%s fdcan=%s uart_dropped=%lu tx_errors=%lu\r\n",
                 sensors_ready ? "ok" : "fail", fdcan_ready ? "ok" : "fail",
                 (unsigned long)diagnostics.dropped_bytes,
                 (unsigned long)diagnostics.tx_errors);
  (void)diagnostics_uart_write(message);
}

void diagnostics_uart_report_startup_failure(const char *subsystem, uint32_t code)
{
  char message[DIAGNOSTICS_UART_MESSAGE_MAX + 1U];

  (void)snprintf(message, sizeof(message), "OAMR startup failure %s code=0x%08lX\r\n",
                 subsystem == NULL ? "unknown" : subsystem, (unsigned long)code);
  (void)diagnostics_uart_write(message);
}

void diagnostics_uart_tx_complete(UART_HandleTypeDef *huart)
{
  uint16_t sent;

  if ((huart == NULL) || (huart != diagnostics.uart) || !diagnostics.active)
  {
    return;
  }
  sent = (uint16_t)(huart->TxXferSize - huart->TxXferCount);
  diagnostics.read_index = (uint16_t)((diagnostics.read_index + sent) % DIAGNOSTICS_UART_TX_CAPACITY);
  diagnostics_send_next();
}

void diagnostics_uart_error(UART_HandleTypeDef *huart)
{
  uint16_t discarded;

  if ((huart == NULL) || (huart != diagnostics.uart) || !diagnostics.active)
  {
    return;
  }
  diagnostics_increment(&diagnostics.tx_errors);
  discarded = (uint16_t)(huart->TxXferSize - huart->TxXferCount);
  diagnostics.read_index = (uint16_t)((diagnostics.read_index + discarded) % DIAGNOSTICS_UART_TX_CAPACITY);
  diagnostics.active = false;
  diagnostics_send_next();
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  diagnostics_uart_tx_complete(huart);
}
