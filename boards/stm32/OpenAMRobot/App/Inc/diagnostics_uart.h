#ifndef DIAGNOSTICS_UART_H
#define DIAGNOSTICS_UART_H

#include "main.h"
#include "oamr/transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One producer (application context), one consumer (USART3 interrupt). */
#define DIAGNOSTICS_UART_TX_CAPACITY 1024U
#define DIAGNOSTICS_UART_MESSAGE_MAX 160U

/* Read and clear the RCC reset flags before HAL or peripheral initialization. */
uint32_t diagnostics_uart_capture_reset_flags(void);

/* Attach an initialized USART3 handle. No GPIO, clocks, or safety state change. */
bool diagnostics_uart_init(UART_HandleTypeDef *huart, uint32_t reset_flags);

/* Queue at most one complete bounded message. Returns false if truncated, the
 * queue lacks room, or the diagnostic UART is unavailable; never waits for TX. */
bool diagnostics_uart_write(const char *text);

/* Atomically queue one complete binary record, or no bytes when it cannot fit.
 * A nonempty record is bounded only by the existing ring. No text conversion,
 * retry, wait, or partial enqueue occurs. Accepted means queued, not delivered. */
bool diagnostics_uart_write_bytes(const void *data, size_t size);

/* PR 3 transport binding. The callback reports the whole offered record as
 * transferred only when the adapter accepted it atomically; otherwise it returns
 * WOULD_BLOCK with zero transferred. No physical-delivery acknowledgement. */
oamr_transport_result_t diagnostics_uart_transport_write(
    void *context, const uint8_t *data, size_t data_size, size_t *transferred);
void diagnostics_uart_poll(void);
void diagnostics_uart_report_boot(bool sensors_ready, bool fdcan_ready);
void diagnostics_uart_report_startup_failure(const char *subsystem, uint32_t code);

/* HAL callback dispatch. Only the USART3 handle is consumed. */
void diagnostics_uart_tx_complete(UART_HandleTypeDef *huart);
void diagnostics_uart_error(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* DIAGNOSTICS_UART_H */
