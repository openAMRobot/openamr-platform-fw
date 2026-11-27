/* Narrow STM32 register/HAL fake for production fault_capture.c tests. */
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
    volatile uint32_t RSR;
} RCC_TypeDef;

typedef struct {
    volatile uint32_t CFSR;
    volatile uint32_t HFSR;
    volatile uint32_t DFSR;
    volatile uint32_t MMFAR;
    volatile uint32_t BFAR;
} SCB_TypeDef;

typedef struct {
    volatile uint32_t CR2;
} PWR_TypeDef;

extern RCC_TypeDef *RCC;
extern SCB_TypeDef *SCB;
extern PWR_TypeDef *PWR;
extern uint32_t fault_test_bkpram_clock_calls;
extern uint32_t fault_test_backup_access_calls;
extern uint32_t fault_test_backup_disable_calls;
extern HAL_StatusTypeDef fault_test_regulator_status;
extern uint32_t fault_test_regulator_calls;

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
#define PWR_FLAG_BRR UINT32_C(0x0000000f)
#define PWR_CR2_BRRDY UINT32_C(0x00000010)

#define __HAL_RCC_BKPRAM_CLK_ENABLE() (fault_test_bkpram_clock_calls++)
#define __HAL_PWR_GET_FLAG(flag) ((flag) == PWR_FLAG_BRR ? \
    ((PWR->CR2 & PWR_CR2_BRRDY) == PWR_CR2_BRRDY) : 0U)

void HAL_PWR_EnableBkUpAccess(void);
void HAL_PWR_DisableBkUpAccess(void);
HAL_StatusTypeDef HAL_PWREx_EnableBkUpReg(void);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_H */
