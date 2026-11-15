/* SPDX-License-Identifier: MIT */
/*
 * Transmit gate of CAN2 (FDCAN2, Daly BMS) and CAN3 (FDCAN3, CTL-017 lift).
 *
 * The telemetry image is receive-only on both buses. tlm_can_aux_tx() is the
 * only function in the image that takes a frame for either of them, and it
 * refuses every frame: it counts it and returns false. It has no path to the
 * HAL at all (this file and tlm_can_aux_tx.c include no HAL header), and the
 * post-link guard (cmake/check_image.cmake) fails the build when any source
 * other than tlm_can1_tx.c names a HAL transmit function. The host test
 * tests/host/telemetry_image/test_can_aux_tx.c checks the refusal for every
 * kind of frame, including the CAN1 upload request that CAN1 would accept.
 *
 * Nothing in the image calls it today. It exists so that a future sender has
 * exactly one place to go through, and that place says no.
 */
#ifndef TLM_CAN_AUX_TX_H
#define TLM_CAN_AUX_TX_H

#include <stdbool.h>
#include <stdint.h>

#include "oamr/co_frame.h"

typedef enum
{
    TLM_CAN_AUX_BMS = 0,  /* CAN2, FDCAN2 */
    TLM_CAN_AUX_LIFT = 1, /* CAN3, FDCAN3 */
    TLM_CAN_AUX_N = 2
} tlm_can_aux_bus_t;

/* Always false; every call is counted as refused for its bus (an unknown bus
 * index is counted for both). */
bool tlm_can_aux_tx(tlm_can_aux_bus_t bus, const oamr_co_frame_t *f);
uint32_t tlm_can_aux_tx_refused(tlm_can_aux_bus_t bus);

#endif /* TLM_CAN_AUX_TX_H */
