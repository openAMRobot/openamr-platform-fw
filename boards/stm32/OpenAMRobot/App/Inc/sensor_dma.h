#ifndef SENSOR_DMA_H
#define SENSOR_DMA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "oamr/tfluna.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SENSOR_DMA_BUFFER_SIZE 256U

typedef enum
{
  SENSOR_DMA_USART2 = 0,
  SENSOR_DMA_USART6,
  SENSOR_DMA_CHANNEL_COUNT
} sensor_dma_channel_t;

typedef struct
{
  uint32_t half_events;
  uint32_t full_events;
  uint32_t start_errors;
  uint32_t uart_dma_errors;
  uint32_t last_uart_error;
  uint64_t last_poll_us;
  bool started;
  bool configured;
} sensor_dma_status_t;

typedef struct
{
  const uint8_t *buffer;
  size_t buffer_size;
  size_t write_index;
  sensor_dma_status_t status;
} sensor_dma_snapshot_t;

/* Configure units explicitly before sensor_dma_init when possible. There is no
 * implicit CM/MM default. Configuration initializes only parser/cursor state;
 * it does not write commands to the physical sensor. */
bool sensor_dma_configure_tfluna(sensor_dma_channel_t channel,
                                 oamr_tfl_unit_t unit);

/* Call after GPIO, DMA and both UART peripherals have been initialized. */
bool sensor_dma_init(void);

/* Snapshot DMA cursors and parse available bytes in application context. */
void sensor_dma_poll(uint32_t now_ms);

bool sensor_dma_snapshot(sensor_dma_channel_t channel,
                         sensor_dma_snapshot_t *snapshot);

/* Latest decoded frame and its application timestamp. This is not a freshness
 * or measurement-health judgment. The sequence identifies repeated delivery. */
bool sensor_dma_latest_tfluna(sensor_dma_channel_t channel,
                              oamr_tfl_sample_t *sample,
                              uint64_t *timestamp_us);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_DMA_H */
