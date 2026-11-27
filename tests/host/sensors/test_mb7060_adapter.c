/* SPDX-License-Identifier: MIT */
#include "test_harness.h"

#include "main.h"
#include "mb7060_sensor.h"

#include <string.h>

UART_HandleTypeDef huart4 = { UART4, 0U };
UART_HandleTypeDef huart5 = { UART5, 0U };
UART_HandleTypeDef huart7 = { UART7, 0U };
UART_HandleTypeDef huart8 = { UART8, 0U };
TIM_HandleTypeDef htim13 = { TIM13, 0U, 0U };
static bool timer_ok = true;
static bool pin_high;
static unsigned int timer_starts;
static unsigned int rx_starts;

HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *data,
                                      uint16_t length)
{
    (void)uart;
    (void)data;
    CHECK(length == 1U);
    rx_starts++;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_OC_Start_IT(TIM_HandleTypeDef *timer, uint32_t channel)
{
    (void)timer;
    CHECK(channel == TIM_CHANNEL_1);
    timer_starts++;
    return timer_ok ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef HAL_TIM_OC_Stop_IT(TIM_HandleTypeDef *timer, uint32_t channel)
{
    (void)timer;
    (void)channel;
    return HAL_OK;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, uint32_t state)
{
    (void)port;
    (void)pin;
    pin_high = state == GPIO_PIN_SET;
}

void mb7060_sensor_uart_rx_complete(UART_HandleTypeDef *huart);
void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim);

static void trigger_pulses_and_receives(void)
{
    static const char reading[] = "R123\r";
    mb7060_sensor_status_t status;
    size_t i;

    CHECK(mb7060_sensor_init(120000U, 10000U));
    CHECK(rx_starts == 4U);
    mb7060_sensor_poll();
    CHECK(pin_high);
    CHECK(timer_starts == 1U);
    HAL_TIM_OC_DelayElapsedCallback(&htim13);
    CHECK(!pin_high);
    for (i = 0U; i < strlen(reading); ++i) {
        mb7060_test_inject_rx_byte(&huart4, (uint8_t)reading[i]);
        mb7060_sensor_uart_rx_complete(&huart4);
    }
    CHECK(mb7060_sensor_status(0U, &status));
    CHECK(status.available);
    CHECK(status.sample.range_cm == 123U);
}

static void timer_failure_does_not_confirm_reading(void)
{
    mb7060_sensor_status_t status;

    timer_ok = false;
    CHECK(mb7060_sensor_init(120000U, 10000U));
    mb7060_sensor_poll();
    CHECK(!pin_high);
    CHECK(mb7060_sensor_status(0U, &status));
    CHECK(status.timer_errors == 1U);
    CHECK(!status.available);
    timer_ok = true;
}

static void timebase_wrap_is_extended(void)
{
    htim13.counter = UINT16_MAX - 2U;
    CHECK(mb7060_sensor_init(120000U, 10000U));
    (void)mb7060_sensor_now_us();
    htim13.counter = 1U;
    CHECK(mb7060_sensor_now_us() >= UINT64_C(3));
}

int main(void)
{
    RUN(trigger_pulses_and_receives);
    RUN(timer_failure_does_not_confirm_reading);
    RUN(timebase_wrap_is_extended);
    return TEST_END();
}
