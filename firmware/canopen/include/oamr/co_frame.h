/* SPDX-License-Identifier: MIT */
/*
 * Classic CAN frame used by the CANopen client pieces in firmware/canopen.
 *
 * Same fields, order and meaning as oamr_can_frame_t of the portable core
 * proposed in platform-fw #44, so a board port can pass frames through with a
 * field copy. Kept separate so this directory builds on the board baseline
 * (platform-fw #22) alone. Pure C11: no HAL, no registers, no allocation.
 */
#ifndef OAMR_CO_FRAME_H
#define OAMR_CO_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t id;      /* 11-bit or 29-bit identifier */
    bool extended;    /* true for a 29-bit identifier; CANopen here uses 11-bit only */
    uint8_t dlc;      /* 0..8; anything above 8 is malformed for classic CAN */
    uint8_t data[8];
} oamr_co_frame_t;

/* Caller-supplied transmit function. Must not block. Returns false when the
 * frame could not be queued (TX FIFO full, bus-off, not started). */
typedef bool (*oamr_co_send_fn)(void *ctx, const oamr_co_frame_t *frame);

static inline uint16_t oamr_co_get_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static inline uint32_t oamr_co_get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void oamr_co_put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

#ifdef __cplusplus
}
#endif

#endif /* OAMR_CO_FRAME_H */
