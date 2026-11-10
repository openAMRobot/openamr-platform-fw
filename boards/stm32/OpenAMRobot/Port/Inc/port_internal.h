/* SPDX-License-Identifier: MIT */
/* Port-internal interfaces between the port source files. */
#ifndef PORT_INTERNAL_H
#define PORT_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "oamr/can.h"
#include "oamr/clock.h"
#include "oamr/icm42688.h"
#include "oamr/reset_cause.h"
#include "oamr/transport.h"
#include "stm32h7xx_hal.h"

/* port_clock.c: TIM2 free-running at 1 MHz, extended to 64 bits. */
void port_clock_init(void);
uint64_t port_clock_now_us(void *ctx);
extern const oamr_clock_t port_clock;

/* port_watchdog.c */
oamr_reset_cause_t port_reset_cause(void);
void port_iwdg_start(uint32_t timeout_ms);
void port_iwdg_feed(void);

/* port_can.c */
oamr_can_tx_t port_can_tx(FDCAN_HandleTypeDef *h);
/* Pops one received frame; returns false when the RX FIFO is empty. */
bool port_can_receive(FDCAN_HandleTypeDef *h, oamr_can_frame_t *f);
bool port_can_bus_off(FDCAN_HandleTypeDef *h);
void port_can_restart(FDCAN_HandleTypeDef *h);

/* port_spi.c */
oamr_spi_t port_imu_spi(void);

/* port_transport.c: USART3 on the ST-LINK virtual COM port (PD8 TX, PD9 RX). */
oamr_transport_t port_serial_transport(void);

/* Control tick timer (TIM6). */
void port_tick_timer_start(uint32_t period_us);

#endif /* PORT_INTERNAL_H */
