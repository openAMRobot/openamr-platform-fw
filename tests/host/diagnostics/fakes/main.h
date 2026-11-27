/* Narrow STM32 UART/RCC interface for diagnostics adapter host tests only. */
#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_OK = 0,
    HAL_ERROR = 1,
    HAL_BUSY = 2,
    HAL_TIMEOUT = 3
} HAL_StatusTypeDef;

typedef struct {
    uint32_t reserved;
} USART_TypeDef;

typedef struct {
    volatile uint32_t RSR;
} RCC_TypeDef;

typedef struct {
    USART_TypeDef *Instance;
    const uint8_t *pTxBuffPtr;
    uint16_t TxXferSize;
    volatile uint16_t TxXferCount;
    volatile uint32_t gState;
} UART_HandleTypeDef;

#define USART3 ((USART_TypeDef *)(uintptr_t)UINT32_C(0x1003))
#define HAL_UART_STATE_READY UINT32_C(0x00000020)
#define HAL_UART_STATE_BUSY UINT32_C(0x00000024)

#define RCC_RSR_RMVF UINT32_C(0x00010000)
#define RCC_RSR_CPURSTF UINT32_C(0x00020000)
#define RCC_RSR_D1RSTF UINT32_C(0x00080000)
#define RCC_RSR_D2RSTF UINT32_C(0x00100000)
#define RCC_RSR_BORRSTF UINT32_C(0x00200000)
#define RCC_RSR_PINRSTF UINT32_C(0x00400000)
#define RCC_RSR_PORRSTF UINT32_C(0x00800000)
#define RCC_RSR_SFTRSTF UINT32_C(0x01000000)
#define RCC_RSR_IWDG1RSTF UINT32_C(0x04000000)
#define RCC_RSR_WWDG1RSTF UINT32_C(0x10000000)
#define RCC_RSR_LPWRRSTF UINT32_C(0x40000000)

extern RCC_TypeDef *RCC;
extern uint32_t diagnostics_test_primask;

static inline uint32_t __get_PRIMASK(void)
{
    return diagnostics_test_primask;
}

static inline void __disable_irq(void)
{
    diagnostics_test_primask = 1u;
}

static inline void __set_PRIMASK(uint32_t primask)
{
    diagnostics_test_primask = primask;
}

#define __HAL_RCC_CLEAR_RESET_FLAGS() (RCC->RSR |= RCC_RSR_RMVF)

HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *huart,
                                       const uint8_t *data, uint16_t size);

/* The production adapter supplies this weak-HAL replacement for the test link. */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_H */
