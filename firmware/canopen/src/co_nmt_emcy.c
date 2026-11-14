/* SPDX-License-Identifier: MIT */
#include "oamr/co_nmt_emcy.h"

#include <string.h>

#include "oamr/co_cobid.h"

oamr_co_dec_t oamr_co_decode_heartbeat(const oamr_co_frame_t *f, uint8_t node_id,
                                       oamr_co_nmt_state_t *state)
{
    if (f == 0 || !oamr_co_node_id_valid(node_id) || f->extended ||
        f->id != OAMR_CO_COB_HEARTBEAT_BASE + node_id) {
        return OAMR_CO_DEC_NOT_FOR_US;
    }
    if (f->dlc != 1u) {
        return OAMR_CO_DEC_MALFORMED;
    }
    const uint8_t s = f->data[0];
    switch (s) {
    case OAMR_CO_NMT_BOOTUP:
    case OAMR_CO_NMT_STOPPED:
    case OAMR_CO_NMT_OPERATIONAL:
    case OAMR_CO_NMT_PRE_OPERATIONAL:
        if (state != 0) {
            *state = (oamr_co_nmt_state_t)s;
        }
        return OAMR_CO_DEC_OK;
    default:
        return OAMR_CO_DEC_MALFORMED; /* includes the guarding toggle bit 7 */
    }
}

oamr_co_dec_t oamr_co_decode_nmt_command(const oamr_co_frame_t *f, oamr_co_nmt_cmd_t *cmd,
                                         uint8_t *target_node)
{
    if (f == 0 || f->extended || f->id != OAMR_CO_COB_NMT) {
        return OAMR_CO_DEC_NOT_FOR_US;
    }
    if (f->dlc != 2u || f->data[1] > OAMR_CO_NODE_ID_MAX) {
        return OAMR_CO_DEC_MALFORMED;
    }
    switch (f->data[0]) {
    case OAMR_CO_NMT_CMD_START:
    case OAMR_CO_NMT_CMD_STOP:
    case OAMR_CO_NMT_CMD_ENTER_PREOP:
    case OAMR_CO_NMT_CMD_RESET_NODE:
    case OAMR_CO_NMT_CMD_RESET_COMM:
        break;
    default:
        return OAMR_CO_DEC_MALFORMED;
    }
    if (cmd != 0) {
        *cmd = (oamr_co_nmt_cmd_t)f->data[0];
    }
    if (target_node != 0) {
        *target_node = f->data[1];
    }
    return OAMR_CO_DEC_OK;
}

oamr_co_dec_t oamr_co_decode_emcy(const oamr_co_frame_t *f, uint8_t node_id, oamr_co_emcy_t *out)
{
    if (out != 0) {
        memset(out, 0, sizeof(*out));
    }
    if (f == 0 || !oamr_co_node_id_valid(node_id) || f->extended ||
        f->id != OAMR_CO_COB_EMCY_BASE + node_id) {
        return OAMR_CO_DEC_NOT_FOR_US;
    }
    if (f->dlc != 8u) {
        return OAMR_CO_DEC_MALFORMED;
    }
    if (out != 0) {
        out->error_code = oamr_co_get_le16(&f->data[0]);
        out->error_register = f->data[2];
        memcpy(out->manufacturer, &f->data[3], sizeof(out->manufacturer));
    }
    return OAMR_CO_DEC_OK;
}
