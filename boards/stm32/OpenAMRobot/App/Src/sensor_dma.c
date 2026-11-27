#include "sensor_dma.h"

#include "main.h"

#include "oamr/dma_cursor.h"

extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart6;
extern DMA_HandleTypeDef hdma_usart2_rx;
extern DMA_HandleTypeDef hdma_usart6_rx;

/* Ethernet descriptors occupy [0x30000000, 0x300000C0). */
uint8_t sensor_usart2_rx_buffer[SENSOR_DMA_BUFFER_SIZE]
  __attribute__((section(".sensor_dma.usart2_rx"), aligned(32), used));
uint8_t sensor_usart6_rx_buffer[SENSOR_DMA_BUFFER_SIZE]
  __attribute__((section(".sensor_dma.usart6_rx"), aligned(32), used));

typedef struct
{
  UART_HandleTypeDef *uart;
  DMA_HandleTypeDef *dma;
  uint8_t *buffer;
  size_t write_index;
  oamr_dma_cursor_t cursor;
  oamr_tfl_t parser;
  oamr_tfl_sample_t latest;
  uint64_t latest_timestamp_us;
  uint32_t latest_sequence;
  bool latest_valid;
  volatile sensor_dma_status_t status;
} sensor_dma_stream_t;

static sensor_dma_stream_t streams[SENSOR_DMA_CHANNEL_COUNT] =
{
  { .uart = &huart2, .dma = &hdma_usart2_rx, .buffer = sensor_usart2_rx_buffer },
  { .uart = &huart6, .dma = &hdma_usart6_rx, .buffer = sensor_usart6_rx_buffer }
};

static sensor_dma_stream_t *sensor_dma_stream(UART_HandleTypeDef *huart)
{
  if ((huart != NULL) && (huart->Instance == USART2))
  {
    return &streams[SENSOR_DMA_USART2];
  }
  if ((huart != NULL) && (huart->Instance == USART6))
  {
    return &streams[SENSOR_DMA_USART6];
  }
  return NULL;
}

static sensor_dma_stream_t *sensor_dma_channel(sensor_dma_channel_t channel)
{
  return channel < SENSOR_DMA_CHANNEL_COUNT ? &streams[channel] : NULL;
}

bool sensor_dma_configure_tfluna(sensor_dma_channel_t channel,
                                 oamr_tfl_unit_t unit)
{
  sensor_dma_stream_t *stream = sensor_dma_channel(channel);
  oamr_tfl_config_t config = { unit };

  if ((stream == NULL) || stream->status.started ||
      (unit != OAMR_TFL_UNIT_CM && unit != OAMR_TFL_UNIT_MM))
  {
    return false;
  }
  if (oamr_tfl_init(&stream->parser, &config) != OAMR_STATUS_OK)
  {
    stream->status.configured = false;
    return false;
  }
  if (oamr_dma_cursor_init(&stream->cursor, SENSOR_DMA_BUFFER_SIZE, 0U) != OAMR_STATUS_OK)
  {
    stream->status.configured = false;
    return false;
  }
  stream->status.configured = true;
  return true;
}

static bool sensor_dma_start(sensor_dma_stream_t *stream)
{
  HAL_StatusTypeDef status;

  status = HAL_UART_Receive_DMA(stream->uart, stream->buffer,
                                SENSOR_DMA_BUFFER_SIZE);
  if (status == HAL_OK)
  {
    stream->write_index = 0U;
    stream->status.started = true;
    return true;
  }

  stream->status.start_errors++;
  return false;
}

bool sensor_dma_init(void)
{
  bool usart2_started = sensor_dma_start(&streams[SENSOR_DMA_USART2]);
  bool usart6_started = sensor_dma_start(&streams[SENSOR_DMA_USART6]);

  return usart2_started && usart6_started;
}

static void sensor_dma_consume(sensor_dma_stream_t *stream, uint64_t now_us)
{
  oamr_dma_chunk_t chunk;

  if (!stream->status.configured)
  {
    /* Unit confirmation is pending: buffer accounting continues, but no raw
     * bytes are converted into measurement records. */
    return;
  }
  oamr_status_t status;

  status = oamr_dma_cursor_peek(&stream->cursor, &chunk);
  while (status == OAMR_STATUS_OK)
  {
    size_t consumed = 0U;
    oamr_tfl_sample_t sample;

    status = oamr_tfl_consume(&stream->parser, &stream->buffer[chunk.offset],
                              chunk.length, &consumed, &sample);
    if (consumed == 0U)
    {
      /* Prevent a malformed adapter/cursor from spinning without progress. */
      stream->status.uart_dma_errors++;
      (void)oamr_dma_cursor_flush(&stream->cursor);
      oamr_tfl_flush(&stream->parser);
      break;
    }
    (void)oamr_dma_cursor_consume(&stream->cursor, consumed);
    if (status == OAMR_STATUS_OK)
    {
      stream->latest = sample;
      stream->latest_timestamp_us = now_us;
      stream->latest_sequence = sample.sequence;
      stream->latest_valid = true;
    }
    status = oamr_dma_cursor_peek(&stream->cursor, &chunk);
  }

  if (status == OAMR_STATUS_OVERFLOW)
  {
    oamr_tfl_flush(&stream->parser);
    (void)oamr_dma_cursor_flush(&stream->cursor);
  }
}

void sensor_dma_poll(uint32_t now_ms)
{
  size_t i;

  for (i = 0U; i < (size_t)SENSOR_DMA_CHANNEL_COUNT; ++i)
  {
    sensor_dma_stream_t *stream = &streams[i];
    uint32_t remaining = __HAL_DMA_GET_COUNTER(stream->dma);

    if (remaining > SENSOR_DMA_BUFFER_SIZE)
    {
      stream->status.uart_dma_errors++;
    }
    else if (stream->status.started)
    {
      size_t write_index = SENSOR_DMA_BUFFER_SIZE - (size_t)remaining;

      if (write_index == SENSOR_DMA_BUFFER_SIZE)
      {
        write_index = 0U;
      }
      stream->write_index = write_index;
      (void)oamr_dma_cursor_update_ndtr(&stream->cursor, (size_t)remaining);
      sensor_dma_consume(stream, (uint64_t)now_ms * UINT64_C(1000));
    }
    else
    {
      /* No active DMA stream: no new bytes are accepted as measurements. */
    }
    stream->status.last_poll_us = (uint64_t)now_ms * UINT64_C(1000);
  }
}

bool sensor_dma_snapshot(sensor_dma_channel_t channel,
                         sensor_dma_snapshot_t *snapshot)
{
  const sensor_dma_stream_t *stream = sensor_dma_channel(channel);

  if ((stream == NULL) || (snapshot == NULL))
  {
    return false;
  }
  snapshot->buffer = stream->buffer;
  snapshot->buffer_size = SENSOR_DMA_BUFFER_SIZE;
  snapshot->write_index = stream->write_index;
  snapshot->status = stream->status;
  return true;
}

bool sensor_dma_latest_tfluna(sensor_dma_channel_t channel,
                              oamr_tfl_sample_t *sample,
                              uint64_t *timestamp_us)
{
  const sensor_dma_stream_t *stream = sensor_dma_channel(channel);

  if ((stream == NULL) || (sample == NULL) || (timestamp_us == NULL) ||
      !stream->latest_valid || !stream->status.configured)
  {
    return false;
  }
  *sample = stream->latest;
  *timestamp_us = stream->latest_timestamp_us;
  return true;
}

/* These callbacks only record events; they never parse or restart reception. */
void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *huart)
{
  sensor_dma_stream_t *stream = sensor_dma_stream(huart);
  if (stream != NULL)
  {
    stream->status.half_events++;
  }
}

void sensor_dma_uart_rx_complete(UART_HandleTypeDef *huart)
{
  sensor_dma_stream_t *stream = sensor_dma_stream(huart);
  if (stream != NULL)
  {
    stream->status.full_events++;
  }
}

void sensor_dma_uart_error(UART_HandleTypeDef *huart)
{
  sensor_dma_stream_t *stream = sensor_dma_stream(huart);
  if (stream != NULL)
  {
    stream->status.uart_dma_errors++;
    stream->status.last_uart_error = huart->ErrorCode;
    stream->status.started = false;
    oamr_tfl_flush(&stream->parser);
  }
}
