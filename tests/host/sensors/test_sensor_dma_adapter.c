/* SPDX-License-Identifier: MIT */
#include "test_harness.h"

#include "main.h"
#include "sensor_dma.h"

#include <string.h>

UART_HandleTypeDef huart2 = { USART2, 0U };
UART_HandleTypeDef huart6 = { USART6, 0U };
DMA_HandleTypeDef hdma_usart2_rx = { SENSOR_DMA_BUFFER_SIZE };
DMA_HandleTypeDef hdma_usart6_rx = { SENSOR_DMA_BUFFER_SIZE };

static HAL_StatusTypeDef start_status = HAL_OK;
static size_t starts;

HAL_StatusTypeDef HAL_UART_Receive_DMA(UART_HandleTypeDef *uart, uint8_t *buffer,
                                       uint16_t length)
{
    (void)uart;
    CHECK(length == SENSOR_DMA_BUFFER_SIZE);
    CHECK(length == 0U || buffer != NULL);
    starts++;
    return start_status;
}

static const uint8_t golden_frame[] = {
    0x59U, 0x59U, 0x64U, 0U, 0x20U, 0x03U, 0U, 0x08U, 0x41U
};

#ifndef SENSOR_DMA_TEST_CONFIGURED
static void unconfigured_dma_produces_no_measurement(void)
{
    sensor_dma_snapshot_t snapshot;
    oamr_tfl_sample_t sample;
    uint64_t timestamp;

    CHECK(!sensor_dma_latest_tfluna(SENSOR_DMA_USART2, &sample, &timestamp));
    CHECK(sensor_dma_init());
    CHECK(starts == 2U);
    CHECK(!sensor_dma_configure_tfluna(SENSOR_DMA_USART2, OAMR_TFL_UNIT_CM));
    CHECK(sensor_dma_snapshot(SENSOR_DMA_USART2, &snapshot));
    memcpy((void *)snapshot.buffer, golden_frame, sizeof(golden_frame));
    hdma_usart2_rx.remaining = SENSOR_DMA_BUFFER_SIZE - sizeof(golden_frame);
    sensor_dma_poll(100U);
    CHECK(!sensor_dma_latest_tfluna(SENSOR_DMA_USART2, &sample, &timestamp));
}

static void start_failure_is_reported(void)
{
    size_t before = starts;

    start_status = HAL_ERROR;
    CHECK(!sensor_dma_init());
    CHECK(starts == before + 2U);
    start_status = HAL_OK;
}
#else
static void configured_frame_is_decoded_once(void)
{
    sensor_dma_snapshot_t snapshot;
    oamr_tfl_sample_t sample;
    uint64_t timestamp;

    CHECK(sensor_dma_configure_tfluna(SENSOR_DMA_USART2, OAMR_TFL_UNIT_CM));
    CHECK(sensor_dma_init());
    CHECK(sensor_dma_snapshot(SENSOR_DMA_USART2, &snapshot));
    memcpy((void *)snapshot.buffer, golden_frame, sizeof(golden_frame));
    hdma_usart2_rx.remaining = SENSOR_DMA_BUFFER_SIZE - sizeof(golden_frame);
    sensor_dma_poll(101U);
    CHECK(sensor_dma_latest_tfluna(SENSOR_DMA_USART2, &sample, &timestamp));
    CHECK(sample.distance_mm == 1000U);
    CHECK(sample.strength == 800U);
    CHECK(timestamp == UINT64_C(101000));
    sensor_dma_poll(102U);
    CHECK(sensor_dma_latest_tfluna(SENSOR_DMA_USART2, &sample, &timestamp));
    CHECK(sample.sequence == 1U);
}
#endif

int main(void)
{
#ifndef SENSOR_DMA_TEST_CONFIGURED
    RUN(unconfigured_dma_produces_no_measurement);
    RUN(start_failure_is_reported);
#else
    RUN(configured_frame_is_decoded_once);
#endif
    return TEST_END();
}
