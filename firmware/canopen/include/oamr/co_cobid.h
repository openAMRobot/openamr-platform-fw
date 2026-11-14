/* SPDX-License-Identifier: MIT */
/*
 * CiA 301 predefined connection set: COB-ID helpers.
 *
 * Only the predefined (default) COB-IDs are covered. A device that remaps a
 * COB-ID (1014h, 1400h..., 1800h...) needs the caller to use the remapped
 * value; nothing here reads or changes a device's mapping.
 */
#ifndef OAMR_CO_COBID_H
#define OAMR_CO_COBID_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OAMR_CO_NODE_ID_MIN 1u
#define OAMR_CO_NODE_ID_MAX 127u

#define OAMR_CO_COB_NMT 0x000u
#define OAMR_CO_COB_SYNC 0x080u
#define OAMR_CO_COB_EMCY_BASE 0x080u      /* + node id */
#define OAMR_CO_COB_TPDO1_BASE 0x180u     /* + node id, device to client */
#define OAMR_CO_COB_RPDO1_BASE 0x200u
#define OAMR_CO_COB_TPDO2_BASE 0x280u
#define OAMR_CO_COB_RPDO2_BASE 0x300u
#define OAMR_CO_COB_TPDO3_BASE 0x380u
#define OAMR_CO_COB_RPDO3_BASE 0x400u
#define OAMR_CO_COB_TPDO4_BASE 0x480u
#define OAMR_CO_COB_RPDO4_BASE 0x500u
#define OAMR_CO_COB_SDO_TX_BASE 0x580u    /* server (device) to client, + node id */
#define OAMR_CO_COB_SDO_RX_BASE 0x600u    /* client to server (device), + node id */
#define OAMR_CO_COB_HEARTBEAT_BASE 0x700u /* + node id */

typedef enum {
    OAMR_CO_FN_INVALID = 0, /* extended id, out of range, or node id 0 where one is needed */
    OAMR_CO_FN_NMT,
    OAMR_CO_FN_SYNC,
    OAMR_CO_FN_EMCY,
    OAMR_CO_FN_TPDO1,
    OAMR_CO_FN_RPDO1,
    OAMR_CO_FN_TPDO2,
    OAMR_CO_FN_RPDO2,
    OAMR_CO_FN_TPDO3,
    OAMR_CO_FN_RPDO3,
    OAMR_CO_FN_TPDO4,
    OAMR_CO_FN_RPDO4,
    OAMR_CO_FN_SDO_TX,     /* response from a server */
    OAMR_CO_FN_SDO_RX,     /* request to a server */
    OAMR_CO_FN_HEARTBEAT,
    OAMR_CO_FN_OTHER       /* valid 11-bit id outside the predefined set (for example LSS, TIME) */
} oamr_co_function_t;

static inline bool oamr_co_node_id_valid(uint8_t node_id)
{
    return node_id >= OAMR_CO_NODE_ID_MIN && node_id <= OAMR_CO_NODE_ID_MAX;
}

/* COB-ID for base + node id; returns 0 for an invalid node id (0 is never a
 * valid node-addressed COB-ID). */
static inline uint32_t oamr_co_cobid(uint32_t base, uint8_t node_id)
{
    return oamr_co_node_id_valid(node_id) ? base + node_id : 0u;
}

/* Classify an identifier against the predefined connection set. node_id is
 * set to the node part for node-addressed functions, else to 0. */
oamr_co_function_t oamr_co_classify(uint32_t id, bool extended, uint8_t *node_id);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_CO_COBID_H */
