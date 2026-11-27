/* Shared HAL UART callback dispatch. DMA callbacks remain event-only; MB7060
 * receives one byte and immediately rearms its UART for the next byte. */
#include "main.h"
#include "diagnostics_uart.h"

void sensor_dma_uart_rx_complete(UART_HandleTypeDef *huart);
void sensor_dma_uart_error(UART_HandleTypeDef *huart);
void mb7060_sensor_uart_rx_complete(UART_HandleTypeDef *huart);
void mb7060_sensor_uart_error(UART_HandleTypeDef *huart);

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart == NULL)
  {
    return;
  }
  if (huart->Instance == USART2 || huart->Instance == USART6)
  {
    sensor_dma_uart_rx_complete(huart);
  }
  else
  {
    mb7060_sensor_uart_rx_complete(huart);
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart == NULL)
  {
    return;
  }
  if (huart->Instance == USART3)
  {
    diagnostics_uart_error(huart);
  }
  else if (huart->Instance == USART2 || huart->Instance == USART6)
  {
    sensor_dma_uart_error(huart);
  }
  else
  {
    mb7060_sensor_uart_error(huart);
  }
}
