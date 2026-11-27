#include "mb7060_sensor.h"

#include "main.h"

#include <stddef.h>

extern UART_HandleTypeDef huart4;
extern UART_HandleTypeDef huart5;
extern UART_HandleTypeDef huart7;
extern UART_HandleTypeDef huart8;
extern TIM_HandleTypeDef htim13;

typedef struct
{
  UART_HandleTypeDef *uart;
  GPIO_TypeDef *trigger_port;
  uint16_t trigger_pin;
  oamr_mb_t parser;
  mb7060_sensor_status_t status;
  uint8_t rx_byte;
  bool receiving;
} mb7060_channel_t;

static mb7060_channel_t channels[MB7060_SENSOR_COUNT] =
{
  { .uart = &huart4, .trigger_port = GPIOE, .trigger_pin = GPIO_PIN_2 },
  { .uart = &huart5, .trigger_port = GPIOE, .trigger_pin = GPIO_PIN_3 },
  { .uart = &huart7, .trigger_port = GPIOG, .trigger_pin = GPIO_PIN_2 },
  { .uart = &huart8, .trigger_port = GPIOG, .trigger_pin = GPIO_PIN_3 }
};
static oamr_sensor_scheduler_t scheduler;
static uint8_t pulse_sensor = MB7060_SENSOR_COUNT;
static uint32_t timer_epoch;
static uint16_t timer_last;

#ifdef MB7060_SENSOR_HOST_TEST
void mb7060_test_inject_rx_byte(UART_HandleTypeDef *uart, uint8_t byte)
{
  size_t i;

  for (i = 0U; i < MB7060_SENSOR_COUNT; ++i)
  {
    if (channels[i].uart == uart)
    {
      channels[i].rx_byte = byte;
      return;
    }
  }
}
#endif

uint64_t mb7060_sensor_now_us(void)
{
  uint16_t counter = (uint16_t)__HAL_TIM_GET_COUNTER(&htim13);
  uint32_t epoch = timer_epoch;
  uint16_t elapsed = (uint16_t)(counter - timer_last);

  timer_epoch = epoch + elapsed;
  timer_last = counter;
  return ((uint64_t)timer_epoch * UINT64_C(1000000)) + counter;
}

static bool mb7060_start_rx(mb7060_channel_t *channel)
{
  HAL_StatusTypeDef result = HAL_UART_Receive_IT(channel->uart, &channel->rx_byte, 1U);

  if (result != HAL_OK)
  {
    channel->status.start_errors++;
    return false;
  }
  channel->receiving = true;
  return true;
}

bool mb7060_sensor_init(uint64_t reading_timeout_us, uint64_t gap_us)
{
  oamr_sensor_scheduler_config_t config = {
    .sensor_count = MB7060_SENSOR_COUNT,
    .reading_timeout_us = reading_timeout_us,
    .gap_us = gap_us
  };
  size_t i;

  for (i = 0U; i < MB7060_SENSOR_COUNT; ++i)
  {
    oamr_mb_init(&channels[i].parser);
    channels[i].status.available = false;
    if (!mb7060_start_rx(&channels[i]))
    {
      return false;
    }
  }
  if (oamr_sensor_scheduler_init(&scheduler, &config) != OAMR_STATUS_OK)
  {
    return false;
  }
  timer_last = (uint16_t)__HAL_TIM_GET_COUNTER(&htim13);
  return true;
}

static void mb7060_trigger(uint8_t sensor, uint64_t now_us)
{
  mb7060_channel_t *channel = &channels[sensor];

  if (pulse_sensor != MB7060_SENSOR_COUNT)
  {
    channel->status.rejected_triggers++;
    return;
  }
  oamr_mb_arm(&channel->parser);
  HAL_GPIO_WritePin(channel->trigger_port, channel->trigger_pin, GPIO_PIN_RESET);
  __HAL_TIM_SET_COMPARE(&htim13, TIM_CHANNEL_1,
                        (uint32_t)((uint16_t)__HAL_TIM_GET_COUNTER(&htim13) + MB7060_TRIGGER_PULSE_US));
  HAL_GPIO_WritePin(channel->trigger_port, channel->trigger_pin, GPIO_PIN_SET);
  if (HAL_TIM_OC_Start_IT(&htim13, TIM_CHANNEL_1) != HAL_OK)
  {
    channel->status.timer_errors++;
    HAL_GPIO_WritePin(channel->trigger_port, channel->trigger_pin, GPIO_PIN_RESET);
    oamr_mb_disarm(&channel->parser);
    return;
  }
  pulse_sensor = sensor;
  if (oamr_sensor_scheduler_triggered(&scheduler, sensor, now_us) != OAMR_STATUS_OK)
  {
    (void)HAL_TIM_OC_Stop_IT(&htim13, TIM_CHANNEL_1);
    HAL_GPIO_WritePin(channel->trigger_port, channel->trigger_pin, GPIO_PIN_RESET);
    oamr_mb_disarm(&channel->parser);
    pulse_sensor = MB7060_SENSOR_COUNT;
    channel->status.rejected_triggers++;
  }
}

void mb7060_sensor_poll(void)
{
  uint64_t now_us = mb7060_sensor_now_us();
  oamr_sensor_decision_t decision = oamr_sensor_scheduler_step(&scheduler, now_us);

  if (decision.action == OAMR_SENSOR_STEP_TRIGGER)
  {
    mb7060_trigger(decision.sensor, now_us);
  }
  else if (decision.action == OAMR_SENSOR_STEP_TIMEOUT)
  {
    oamr_mb_disarm(&channels[decision.sensor].parser);
  }
  else
  {
    /* No action needed. */
  }
}

bool mb7060_sensor_status(uint8_t sensor, mb7060_sensor_status_t *status)
{
  if (sensor >= MB7060_SENSOR_COUNT || status == NULL)
  {
    return false;
  }
  *status = channels[sensor].status;
  return true;
}

static void mb7060_uart_byte(UART_HandleTypeDef *huart)
{
  size_t i;

  for (i = 0U; i < MB7060_SENSOR_COUNT; ++i)
  {
    mb7060_channel_t *channel = &channels[i];
    oamr_mb_sample_t sample;

    if (channel->uart != huart)
    {
      continue;
    }
    if (oamr_mb_push(&channel->parser, channel->rx_byte, &sample) == OAMR_MB_PUSH_SAMPLE)
    {
      uint64_t now_us = mb7060_sensor_now_us();

      if (oamr_sensor_scheduler_reading(&scheduler, (uint8_t)i, now_us) == OAMR_STATUS_OK)
      {
        channel->status.sample = sample;
        channel->status.timestamp_us = now_us;
        channel->status.available = true;
      }
    }
    (void)mb7060_start_rx(channel);
    return;
  }
}

void mb7060_sensor_uart_rx_complete(UART_HandleTypeDef *huart)
{
  mb7060_uart_byte(huart);
}

void mb7060_sensor_uart_error(UART_HandleTypeDef *huart)
{
  size_t i;

  for (i = 0U; i < MB7060_SENSOR_COUNT; ++i)
  {
    if (channels[i].uart == huart)
    {
      channels[i].status.uart_errors++;
      channels[i].receiving = false;
      oamr_mb_flush(&channels[i].parser);
      (void)mb7060_start_rx(&channels[i]);
      return;
    }
  }
}

void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM13 && pulse_sensor < MB7060_SENSOR_COUNT)
  {
    mb7060_channel_t *channel = &channels[pulse_sensor];

    HAL_GPIO_WritePin(channel->trigger_port, channel->trigger_pin, GPIO_PIN_RESET);
    (void)HAL_TIM_OC_Stop_IT(&htim13, TIM_CHANNEL_1);
    pulse_sensor = MB7060_SENSOR_COUNT;
  }
}
