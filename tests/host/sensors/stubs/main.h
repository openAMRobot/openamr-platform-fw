/* Minimal board HAL fake for host adapter tests only. */
#ifndef MAIN_H
#define MAIN_H

#include <stddef.h>
#include <stdint.h>

typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
typedef struct {
    void *Instance;
    uint32_t ErrorCode;
} UART_HandleTypeDef;
typedef struct {
    uint32_t remaining;
} DMA_HandleTypeDef;

#define USART2 ((void *)(uintptr_t)UINT64_C(0x2))
#define USART6 ((void *)(uintptr_t)UINT64_C(0x6))
#define __HAL_DMA_GET_COUNTER(handle) ((handle)->remaining)

HAL_StatusTypeDef HAL_UART_Receive_DMA(UART_HandleTypeDef *uart,
                                       uint8_t *buffer, uint16_t length);

#endif
