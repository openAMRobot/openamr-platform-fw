/* SPDX-License-Identifier: MIT */
/*
 * The only path from this image to the CAN1 bus.
 *
 * tlm_can1_tx() hands a frame to HAL_FDCAN_AddMessageToTxFifoQ only when it
 * is an SDO initiate-upload request to the drive node: standard identifier
 * 600h + TLM_DRIVE_NODE_ID, DLC 8, byte 0 = 40h, bytes 4..7 zero. Every other
 * frame (an SDO download, an NMT command, a PDO, SYNC, any other identifier)
 * is refused and counted before the HAL is reached. The host test build in
 * tests/host/telemetry_image proves that with a recording HAL stub.
 *
 * This file includes main.h for the HAL types only; the test build supplies
 * its own main.h.
 */
#ifndef TLM_CAN1_TX_H
#define TLM_CAN1_TX_H

#include <stdbool.h>
#include <stdint.h>

#include "oamr/co_frame.h"

typedef struct
{
    uint32_t sent;    /* accepted by the HAL */
    uint32_t failed;  /* upload request the HAL did not take, or CAN1 not started */
    uint32_t refused; /* not an upload request: never reached the HAL */
} tlm_can1_tx_counters_t;

/* Pure: is f an SDO initiate-upload request to the drive node? */
bool tlm_can1_tx_is_upload_request(const oamr_co_frame_t *f);
/* Set once FDCAN1 has started; until then every request counts as failed. */
void tlm_can1_tx_set_started(bool started);
/* Send callback of the upload scheduler (oamr_co_upl_config_t.send). */
bool tlm_can1_tx(void *ctx, const oamr_co_frame_t *f);
tlm_can1_tx_counters_t tlm_can1_tx_counters(void);

#endif /* TLM_CAN1_TX_H */
