/* SPDX-License-Identifier: MIT */
/* Internal interfaces of the telemetry image. */
#ifndef TLM_H
#define TLM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tlm_params.h"
#include "tlm_status.h"

/* ---- board (cubemx_main_reuse.c) ---- */
/* Generated clock, GPIO, FDCAN1 and sensor UART initialisation. Returns the
 * raw RCC_RSR value read before the reset flags were cleared. */
uint32_t tlm_board_init(void);

/* ---- clock (tlm_time.c) ---- */
/* Monotonic microseconds since HAL_Init(), 64 bit, callable from tasks and
 * from interrupts. Resolution is the SysTick counter. */
uint64_t tlm_time_us(void);
void tlm_time_tick_isr(void);

/* ---- console (tlm_console.c) ---- */
bool tlm_console_init(void);
/* Blocking write; false when the bytes did not leave within the timeout. */
bool tlm_console_write(const char *data, size_t len);
/* For the fatal path only: no HAL state, no interrupts needed. */
void tlm_console_write_raw(const char *text);

/* ---- CAN1 (tlm_can1.c) ---- */
void tlm_can1_init(void);
void tlm_can1_task(void *arg);
void tlm_can1_snapshot(tlm_drive_status_t *out, uint64_t now_us);

/* ---- sensors (tlm_sensors.c) ---- */
void tlm_sensors_init(void);
void tlm_sensors_task(void *arg);
void tlm_sensors_snapshot(tlm_sensors_status_t *out, uint64_t now_us);

/* ---- reporter (tlm_report.c) ---- */
void tlm_report_task(void *arg);
void tlm_report_banner(uint32_t rcc_rsr, const tlm_boot_id_t *boot);

/* ---- fatal stop: prints one line and spins. Never resets the MCU. ---- */
void tlm_fatal(const char *what);

#endif /* TLM_H */
