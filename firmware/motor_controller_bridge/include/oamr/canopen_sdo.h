/* SPDX-License-Identifier: MIT */
/*
 * Minimal CANopen SDO client (CiA 301): expedited upload and download only,
 * one transfer outstanding, with timeout and abort handling. Non-blocking:
 * the owner feeds received frames into oamr_sdo_on_frame() and calls
 * oamr_sdo_poll() every tick for the timeout.
 */
#ifndef OAMR_CANOPEN_SDO_H
#define OAMR_CANOPEN_SDO_H

#include <stdbool.h>
#include <stdint.h>

#include "oamr/can.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OAMR_SDO_TX_BASE 0x600u  /* client -> server, + node id */
#define OAMR_SDO_RX_BASE 0x580u  /* server -> client, + node id */
#define OAMR_SDO_ABORT_TIMEOUT 0x05040000u

typedef enum {
    OAMR_SDO_IDLE = 0,
    OAMR_SDO_BUSY,
    OAMR_SDO_DONE,
    OAMR_SDO_ABORTED,   /* server aborted; abort_code holds the reason */
    OAMR_SDO_TIMEOUT,   /* no answer within timeout_us; an abort was sent */
    OAMR_SDO_TX_FAILED  /* request could not be queued */
} oamr_sdo_state_t;

typedef struct {
    uint8_t node_id;
    uint64_t timeout_us;
    oamr_sdo_state_t state;
    bool is_read;
    uint16_t index;
    uint8_t subindex;
    uint8_t size;           /* bytes for a write; bytes received for a read */
    uint32_t value;         /* write value, or value read */
    uint32_t abort_code;
    uint64_t started_us;
} oamr_sdo_client_t;

void oamr_sdo_init(oamr_sdo_client_t *c, uint8_t node_id, uint64_t timeout_us);
bool oamr_sdo_start_read(oamr_sdo_client_t *c, const oamr_can_tx_t *tx, uint16_t index,
                         uint8_t subindex, uint64_t now_us);
/* size must be 1, 2 or 4. */
bool oamr_sdo_start_write(oamr_sdo_client_t *c, const oamr_can_tx_t *tx, uint16_t index,
                          uint8_t subindex, uint32_t value, uint8_t size, uint64_t now_us);
/* Returns true if the frame was an SDO response for this client. */
bool oamr_sdo_on_frame(oamr_sdo_client_t *c, const oamr_can_frame_t *f);
void oamr_sdo_poll(oamr_sdo_client_t *c, const oamr_can_tx_t *tx, uint64_t now_us);
static inline bool oamr_sdo_finished(const oamr_sdo_client_t *c)
{
    return c->state != OAMR_SDO_IDLE && c->state != OAMR_SDO_BUSY;
}
/* Acknowledge a finished transfer so the next one can start. */
static inline void oamr_sdo_release(oamr_sdo_client_t *c)
{
    c->state = OAMR_SDO_IDLE;
}

#ifdef __cplusplus
}
#endif

#endif /* OAMR_CANOPEN_SDO_H */
