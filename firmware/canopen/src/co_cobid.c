/* SPDX-License-Identifier: MIT */
#include "oamr/co_cobid.h"

oamr_co_function_t oamr_co_classify(uint32_t id, bool extended, uint8_t *node_id)
{
    uint8_t dummy;
    uint8_t *nid = node_id != 0 ? node_id : &dummy;
    *nid = 0;
    if (extended || id > 0x7FFu) {
        return OAMR_CO_FN_INVALID;
    }
    if (id == OAMR_CO_COB_NMT) {
        return OAMR_CO_FN_NMT;
    }
    if (id == OAMR_CO_COB_SYNC) {
        return OAMR_CO_FN_SYNC;
    }
    const uint32_t base = id & 0x780u;
    const uint8_t node = (uint8_t)(id & 0x7Fu);
    if (node == 0u) {
        return OAMR_CO_FN_OTHER; /* function code with node 0: not node-addressed */
    }
    oamr_co_function_t fn;
    switch (base) {
    case OAMR_CO_COB_EMCY_BASE: fn = OAMR_CO_FN_EMCY; break;
    case OAMR_CO_COB_TPDO1_BASE: fn = OAMR_CO_FN_TPDO1; break;
    case OAMR_CO_COB_RPDO1_BASE: fn = OAMR_CO_FN_RPDO1; break;
    case OAMR_CO_COB_TPDO2_BASE: fn = OAMR_CO_FN_TPDO2; break;
    case OAMR_CO_COB_RPDO2_BASE: fn = OAMR_CO_FN_RPDO2; break;
    case OAMR_CO_COB_TPDO3_BASE: fn = OAMR_CO_FN_TPDO3; break;
    case OAMR_CO_COB_RPDO3_BASE: fn = OAMR_CO_FN_RPDO3; break;
    case OAMR_CO_COB_TPDO4_BASE: fn = OAMR_CO_FN_TPDO4; break;
    case OAMR_CO_COB_RPDO4_BASE: fn = OAMR_CO_FN_RPDO4; break;
    case OAMR_CO_COB_SDO_TX_BASE: fn = OAMR_CO_FN_SDO_TX; break;
    case OAMR_CO_COB_SDO_RX_BASE: fn = OAMR_CO_FN_SDO_RX; break;
    case OAMR_CO_COB_HEARTBEAT_BASE: fn = OAMR_CO_FN_HEARTBEAT; break;
    default: return OAMR_CO_FN_OTHER;
    }
    *nid = node;
    return fn;
}
