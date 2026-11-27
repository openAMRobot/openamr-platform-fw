#ifndef MAIN_H
#define MAIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
typedef struct {
    uint32_t Instance;
    uint32_t ErrorCode;
} UART_HandleTypeDef;
typedef struct { uint32_t Instance; uint16_t counter; uint32_t compare; } TIM_HandleTypeDef;
typedef struct { int unused; } GPIO_TypeDef;

#define UART4 4U
#define UART5 5U
#define UART7 7U
#define UART8 8U
#define TIM13 13U
#define TIM_CHANNEL_1 1U
#define GPIO_PIN_2 (1U << 2)
#define GPIO_PIN_3 (1U << 3)
#define GPIO_PIN_RESET 0U
#define GPIO_PIN_SET 1U
#define GPIOE ((GPIO_TypeDef *)(uintptr_t)0U)
#define GPIOG ((GPIO_TypeDef *)(uintptr_t)1U)
#define __HAL_TIM_GET_COUNTER(handle) ((handle)->counter)
#define __HAL_TIM_SET_COMPARE(handle, channel, value) ((handle)->compare = (value))

HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *data,
                                      uint16_t length);
HAL_StatusTypeDef HAL_TIM_OC_Start_IT(TIM_HandleTypeDef *timer, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_OC_Stop_IT(TIM_HandleTypeDef *timer, uint32_t channel);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, uint32_t state);

#endif
