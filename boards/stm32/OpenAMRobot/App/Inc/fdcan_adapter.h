#ifndef FDCAN_ADAPTER_H
#define FDCAN_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "oamr/can_frame.h"
#include "oamr/status.h"
#include "main.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FDCAN_ADAPTER_RX_QUEUE_CAPACITY 8U
#define FDCAN_ADAPTER_POLL_LIMIT 8U

#define FDCAN_ADAPTER_DALY_RX_ID UINT32_C(0x18104001)
#define FDCAN_ADAPTER_DALY_TX_ID UINT32_C(0x18100140)
#define FDCAN_ADAPTER_DALY_DLC 8U

typedef enum
{
  FDCAN_ADAPTER_FDCAN1 = 0,
  FDCAN_ADAPTER_FDCAN2,
  FDCAN_ADAPTER_FDCAN3,
  FDCAN_ADAPTER_BUS_COUNT
} fdcan_adapter_bus_t;

typedef struct
{
  uint32_t start_errors;
  uint32_t rx_events;
  uint32_t rx_frames;
  uint32_t rx_queue_overflows;
  uint32_t rx_fifo_overflows;
  uint32_t rx_malformed_frames;
  uint32_t rx_hal_errors;
  uint32_t error_events;
  uint32_t warning_events;
  uint32_t passive_events;
  uint32_t bus_off_events;
  uint32_t tx_submitted;
  uint32_t tx_complete;
  uint32_t tx_aborted;
  uint32_t tx_rejected;
  uint32_t tx_hal_errors;
  uint32_t last_hal_error;
  uint32_t last_protocol_error;
  uint32_t tx_error_count;
  uint32_t rx_error_count;
  bool started;
  bool warning;
  bool passive;
  bool bus_off;
} fdcan_adapter_diagnostics_t;

/* Call only after all three generated MX_FDCAN*_Init functions have run.
 *
 * FDCAN1/FDCAN3 are bus-monitoring receivers for diagnostics only. Their broad
 * 0x000..0x7FF standard range is deliberate because no exact filter lists have
 * been approved; extended frames are rejected. They cannot transmit or ACK.
 * FDCAN2 accepts only the specified extended Daly response ID. All queues are
 * statically owned by this adapter; there is no dynamic allocation.
 *
 * This initialization starts CAN reception only. It does not queue a request,
 * enable an actuator, or change motor/lift/safety state. Failure to start one
 * bus is recorded and does not prevent independent initialization of the others.
 */
bool fdcan_adapter_init(void);

/* Snapshot hardware error counters/status in foreground context. Recovery is
 * deliberately not automatic: the approved backoff and TX-state policy belong
 * to a separate integration step. now_ms is reserved for that step. */
void fdcan_adapter_poll(uint32_t now_ms);

/* Pop one copied Classical CAN frame in application context. The queue holds
 * raw frames only; no Daly/CANopen decoding is performed here. */
oamr_status_t fdcan_adapter_receive(fdcan_adapter_bus_t bus,
                                    oamr_can_frame_t *frame);

/* FDCAN2-only, nonblocking request submission. The supplied eight bytes are
 * copied into TX message RAM unchanged; this API performs no payload scheduling
 * or interpretation. Success means queued by HAL, not acknowledged on the bus.
 */
oamr_status_t fdcan_adapter_send_daly_request(const uint8_t data[FDCAN_ADAPTER_DALY_DLC]);

bool fdcan_adapter_diagnostics(fdcan_adapter_bus_t bus,
                               fdcan_adapter_diagnostics_t *diagnostics);

/* HAL weak-callback dispatch. Only HAL_FDCAN_IRQHandler should call these from
 * interrupt context; they copy frames/update counters and never decode data. */
void fdcan_adapter_rx_fifo0(FDCAN_HandleTypeDef *hfdcan, uint32_t interrupts);
void fdcan_adapter_tx_complete(FDCAN_HandleTypeDef *hfdcan, uint32_t buffer_indexes);
void fdcan_adapter_tx_aborted(FDCAN_HandleTypeDef *hfdcan, uint32_t buffer_indexes);
void fdcan_adapter_error(FDCAN_HandleTypeDef *hfdcan);
void fdcan_adapter_error_status(FDCAN_HandleTypeDef *hfdcan, uint32_t interrupts);

#ifdef __cplusplus
}
#endif

#endif /* FDCAN_ADAPTER_H */
