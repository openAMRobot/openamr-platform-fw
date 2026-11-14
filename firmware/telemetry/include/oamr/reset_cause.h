/* SPDX-License-Identifier: MIT */
/*
 * Reset-cause decoder: pure function from the raw STM32H723 RCC_RSR value
 * (captured by the board port before it sets RMVF) to the I8 BaseStatus
 * reset_cause enum.
 *
 * PROPOSAL. I8 decision 6 (reset-cause enum) is open; this mapping is for
 * the owner to adopt or change. The enum values mirror BaseStatus.msg of
 * interfaces #38 (rev 4): UNKNOWN 0, POWER_ON 1, BROWNOUT 2, WATCHDOG 3,
 * SOFTWARE 4, PIN 5.
 *
 * RCC_RSR bit positions: RM0468 (STM32H723/733/725/735/730) and the CMSIS
 * header stm32h723xx.h shipped in platform-fw #22: CPURSTF 17, D1RSTF 19,
 * D2RSTF 20, BORRSTF 21, PINRSTF 22, PORRSTF 23, SFTRSTF 24, IWDG1RSTF 26,
 * WWDG1RSTF 28, LPWRRSTF 30. RMVF (16) is a write-only command bit.
 *
 * Hardware behaviour this mapping relies on (RM0468 reset section, to be
 * checked by the owner on the bench): the NRST pin is driven low by every
 * internal reset source, so PINRSTF is set together with SFTRSTF, IWDG1RSTF,
 * WWDG1RSTF and LPWRRSTF; a power-on reset also sets BORRSTF and PINRSTF;
 * CPURSTF accompanies system resets. PINRSTF alone therefore means an
 * external NRST pulse only when no other source flag is set.
 *
 * Priority (most specific first): watchdog, software, low-power error
 * (mapped to UNKNOWN: no I8 value), power-on, brownout, pin. A value with
 * no known flag, a zero value, or a bit outside the documented set is
 * UNKNOWN. All flags are kept in the decoded mask for reset_flags_raw.
 */
#ifndef OAMR_RESET_CAUSE_H
#define OAMR_RESET_CAUSE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RCC_RSR bits, RM0468. */
#define OAMR_RSR_CPURSTF (1u << 17)
#define OAMR_RSR_D1RSTF (1u << 19)
#define OAMR_RSR_D2RSTF (1u << 20)
#define OAMR_RSR_BORRSTF (1u << 21)
#define OAMR_RSR_PINRSTF (1u << 22)
#define OAMR_RSR_PORRSTF (1u << 23)
#define OAMR_RSR_SFTRSTF (1u << 24)
#define OAMR_RSR_IWDG1RSTF (1u << 26)
#define OAMR_RSR_WWDG1RSTF (1u << 28)
#define OAMR_RSR_LPWRRSTF (1u << 30)
#define OAMR_RSR_KNOWN_MASK                                                                        \
    (OAMR_RSR_CPURSTF | OAMR_RSR_D1RSTF | OAMR_RSR_D2RSTF | OAMR_RSR_BORRSTF | OAMR_RSR_PINRSTF |  \
     OAMR_RSR_PORRSTF | OAMR_RSR_SFTRSTF | OAMR_RSR_IWDG1RSTF | OAMR_RSR_WWDG1RSTF |               \
     OAMR_RSR_LPWRRSTF)

/* I8 BaseStatus reset_cause (interfaces #38 rev 4). */
typedef enum {
    OAMR_RESET_CAUSE_UNKNOWN = 0,
    OAMR_RESET_CAUSE_POWER_ON = 1,
    OAMR_RESET_CAUSE_BROWNOUT = 2,
    OAMR_RESET_CAUSE_WATCHDOG = 3,
    OAMR_RESET_CAUSE_SOFTWARE = 4,
    OAMR_RESET_CAUSE_PIN = 5
} oamr_reset_cause_t;

typedef struct {
    oamr_reset_cause_t cause;
    uint32_t flags_raw;      /* the value passed in, for reset_flags_raw */
    bool multiple_sources;   /* more than one source flag besides the implied PIN/CPU/BOR */
    bool unknown_bits;       /* bits outside the documented set were present */
} oamr_reset_decoded_t;

oamr_reset_decoded_t oamr_reset_cause_decode(uint32_t rcc_rsr);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_RESET_CAUSE_H */
