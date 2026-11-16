/* SPDX-License-Identifier: MIT */
/*
 * Classic CAN frame and transmit interface used by the device adapters.
 *
 * Received frames are pushed into an adapter by the board port
 * (adapter_on_frame(...)); adapters only ever transmit through oamr_can_tx_t.
 * send() must not block: it returns false when the TX queue is full or the
 * controller is bus-off.
 */
#ifndef OAMR_CAN_H
#define OAMR_CAN_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t id;      /* 11-bit or 29-bit identifier */
    bool extended;    /* true for a 29-bit identifier */
    uint8_t dlc;      /* 0..8 */
    uint8_t data[8];
} oamr_can_frame_t;

typedef struct {
    bool (*send)(void *ctx, const oamr_can_frame_t *frame);
    void *ctx;
} oamr_can_tx_t;

static inline bool oamr_can_send(const oamr_can_tx_t *tx, const oamr_can_frame_t *frame)
{
    return tx->send(tx->ctx, frame);
}

#ifdef __cplusplus
}
#endif

#endif /* OAMR_CAN_H */
