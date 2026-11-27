#ifndef MB7060_SENSOR_H
#define MB7060_SENSOR_H

#include "oamr/mb7060.h"
#include "oamr/sensor_scheduler.h"

#include <stdbool.h>
#include <stdint.h>

#define MB7060_SENSOR_COUNT 4U
#define MB7060_TRIGGER_PULSE_US 20U

#ifdef MB7060_SENSOR_HOST_TEST
#include "main.h"
void mb7060_test_inject_rx_byte(UART_HandleTypeDef *uart, uint8_t byte);
#endif

typedef struct
{
  oamr_mb_sample_t sample;
  uint64_t timestamp_us;
  bool available;
  uint32_t start_errors;
  uint32_t uart_errors;
  uint32_t timer_errors;
  uint32_t rejected_triggers;
} mb7060_sensor_status_t;

/* Initialize UART RX and the TIM13 microsecond timebase. No trigger is emitted
 * until poll() proposes it. reading_timeout_us/gap_us are caller policy. */
bool mb7060_sensor_init(uint64_t reading_timeout_us, uint64_t gap_us);
void mb7060_sensor_poll(void);
uint64_t mb7060_sensor_now_us(void);
bool mb7060_sensor_status(uint8_t sensor, mb7060_sensor_status_t *status);

#endif
