/* SPDX-License-Identifier: MIT */
/* SPI4 transfer for the ICM-42688-P with software chip select on PE11. */
#include "port_board.h"
#include "port_internal.h"

extern SPI_HandleTypeDef hspi4;

#define IMU_SPI_TIMEOUT_MS 2u

static bool imu_transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    (void)ctx;
    if (len == 0u || len > 0xFFFFu) {
        return false;
    }
    HAL_GPIO_WritePin(PORT_IMU_CS_PORT, PORT_IMU_CS_PIN, GPIO_PIN_RESET);
    const HAL_StatusTypeDef st =
        HAL_SPI_TransmitReceive(&hspi4, (uint8_t *)tx, rx, (uint16_t)len, IMU_SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin(PORT_IMU_CS_PORT, PORT_IMU_CS_PIN, GPIO_PIN_SET);
    return st == HAL_OK;
}

static void imu_delay_us(void *ctx, uint32_t us)
{
    const uint64_t start = port_clock_now_us(ctx);
    while (port_clock_now_us(ctx) - start < us) {
    }
}

oamr_spi_t port_imu_spi(void)
{
    oamr_spi_t s = {imu_transfer, imu_delay_us, NULL};
    return s;
}
