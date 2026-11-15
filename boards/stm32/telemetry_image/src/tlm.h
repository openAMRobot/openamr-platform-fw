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
/* For the fatal path only: no HAL state, no interrupts needed. Each wait
 * for the TX-ready flag takes from *budget (polls); when it is used up the
 * rest of the text is not written. */
void tlm_console_write_raw_bounded(const char *text, uint32_t *budget);
/* Same, with a budget of TLM_FATAL_TX_SPIN_MAX for this text alone. */
void tlm_console_write_raw(const char *text);

/* ---- CAN1 (tlm_can1.c) ---- */
void tlm_can1_init(void);
/* Frame handling: blocks on the receive queue. */
void tlm_can1_rx_task(void *arg);
/* Periodic scheduler tick, TLM_CAN1_TICK_MS on vTaskDelayUntil timing. */
void tlm_can1_tick_task(void *arg);
void tlm_can1_snapshot(tlm_drive_status_t *out, uint64_t now_us);
/* FDCAN1 interrupt 0 wrapper (cubemx_it_reuse.c): budget per entry. */
void tlm_can1_irq_begin(void);
void tlm_can1_irq_end(void);

/* ---- sensors (tlm_sensors.c) ---- */
void tlm_sensors_init(void);
void tlm_sensors_task(void *arg);
void tlm_sensors_snapshot(tlm_sensors_status_t *out, uint64_t now_us);

/* ---- reporter (tlm_report.c) ---- */
void tlm_report_task(void *arg);
void tlm_report_banner(uint32_t rcc_rsr, const tlm_boot_id_t *boot);

/* ---- stack high-water marks (tlm_main.c), words never used ---- */
void tlm_stack_marks(tlm_stack_marks_t *out);

/* ---- fatal stop (tlm_main.c) ----
 * Prints one line with a bounded console wait and halts in a loop with
 * interrupts disabled. The halt is a property of this diagnostic image: it
 * keeps the last record readable on the console for a person at the bench.
 * It is not watchdog behaviour: no watchdog is configured or fed, nothing
 * resets the MCU, and no output is switched. */
void tlm_fatal(const char *what);

#endif /* TLM_H */
