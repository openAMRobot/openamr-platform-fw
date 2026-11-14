/* SPDX-License-Identifier: MIT */
#include "oamr/co_sdo.h"

#include <string.h>

#include "oamr/co_cobid.h"

static void put_header(oamr_co_frame_t *f, uint32_t id, uint8_t cs, uint16_t index, uint8_t subindex)
{
    memset(f, 0, sizeof(*f));
    f->id = id;
    f->extended = false;
    f->dlc = 8u;
    f->data[0] = cs;
    f->data[1] = (uint8_t)(index & 0xFFu);
    f->data[2] = (uint8_t)(index >> 8);
    f->data[3] = subindex;
}

bool oamr_co_sdo_encode_upload_request(uint8_t node_id, uint16_t index, uint8_t subindex,
                                       oamr_co_frame_t *out)
{
    if (out == 0 || !oamr_co_node_id_valid(node_id)) {
        return false;
    }
    put_header(out, OAMR_CO_COB_SDO_RX_BASE + node_id, OAMR_CO_SDO_CCS_UPLOAD_INIT, index, subindex);
    return true;
}

bool oamr_co_sdo_encode_download_expedited(uint8_t node_id, uint16_t index, uint8_t subindex,
                                           uint32_t value, uint8_t size, oamr_co_frame_t *out)
{
    if (out == 0 || !oamr_co_node_id_valid(node_id) || size < 1u || size > 4u || size == 3u) {
        return false;
    }
    /* 001n nnes: ccs 1, n = 4 - size, e = 1, s = 1 */
    const uint8_t n = (uint8_t)(4u - size);
    const uint8_t cs = (uint8_t)(0x20u | (uint8_t)(n << 2) | 0x03u);
    if (size < 4u) {
        value &= (1u << (8u * size)) - 1u;
    }
    put_header(out, OAMR_CO_COB_SDO_RX_BASE + node_id, cs, index, subindex);
    oamr_co_put_le32(&out->data[4], value);
    return true;
}

bool oamr_co_sdo_decode_abort(const oamr_co_frame_t *f, uint32_t cob_id, uint16_t *index,
                              uint8_t *subindex, uint32_t *abort_code)
{
    if (f == 0 || f->extended || f->id != cob_id || f->dlc != 8u || f->data[0] != OAMR_CO_SDO_CS_ABORT) {
        return false;
    }
    if (index != 0) {
        *index = oamr_co_get_le16(&f->data[1]);
    }
    if (subindex != 0) {
        *subindex = f->data[3];
    }
    if (abort_code != 0) {
        *abort_code = oamr_co_get_le32(&f->data[4]);
    }
    return true;
}

oamr_co_sdo_rx_t oamr_co_sdo_decode_upload_response(const oamr_co_frame_t *f, uint8_t node_id,
                                                    uint16_t index, uint8_t subindex,
                                                    oamr_co_sdo_response_t *out)
{
    oamr_co_sdo_response_t tmp;
    oamr_co_sdo_response_t *r = out != 0 ? out : &tmp;
    memset(r, 0, sizeof(*r));
    if (f == 0 || !oamr_co_node_id_valid(node_id) || f->extended ||
        f->id != OAMR_CO_COB_SDO_TX_BASE + node_id) {
        return OAMR_CO_SDO_RX_NOT_FOR_US;
    }
    if (f->dlc != 8u) {
        return OAMR_CO_SDO_RX_MALFORMED; /* CiA 301: SDO frames always carry 8 bytes */
    }
    const uint8_t cs = f->data[0];
    r->index = oamr_co_get_le16(&f->data[1]);
    r->subindex = f->data[3];

    if (cs == OAMR_CO_SDO_CS_ABORT) {
        if (r->index != index || r->subindex != subindex) {
            return OAMR_CO_SDO_RX_WRONG_OBJECT;
        }
        r->abort_code = oamr_co_get_le32(&f->data[4]);
        return OAMR_CO_SDO_RX_ABORT;
    }
    const uint8_t scs = (uint8_t)(cs & 0xE0u);
    if (scs == OAMR_CO_SDO_SCS_DOWNLOAD_INIT || scs == 0x00u || scs == 0x20u) {
        /* download ack, upload segment (with toggle bit) or download segment ack */
        return OAMR_CO_SDO_RX_UNEXPECTED_CS;
    }
    if (scs != OAMR_CO_SDO_SCS_UPLOAD_INIT) {
        return OAMR_CO_SDO_RX_MALFORMED; /* block transfer or reserved specifier */
    }
    if ((cs & 0x10u) != 0u) {
        return OAMR_CO_SDO_RX_MALFORMED; /* bit 4 is reserved (0) in initiate upload */
    }
    if (r->index != index || r->subindex != subindex) {
        return OAMR_CO_SDO_RX_WRONG_OBJECT;
    }
    const bool expedited = (cs & 0x02u) != 0u;
    const bool size_ind = (cs & 0x01u) != 0u;
    if (!expedited) {
        return OAMR_CO_SDO_RX_SEGMENTED;
    }
    uint8_t size = 4u;
    if (size_ind) {
        size = (uint8_t)(4u - ((cs >> 2) & 0x03u));
    } else if ((cs & 0x0Cu) != 0u) {
        return OAMR_CO_SDO_RX_MALFORMED; /* n must be 0 when s = 0 */
    }
    uint32_t v = oamr_co_get_le32(&f->data[4]);
    if (size < 4u) {
        v &= (1u << (8u * size)) - 1u;
    }
    r->size = size;
    r->size_indicated = size_ind;
    r->value = v;
    return OAMR_CO_SDO_RX_OK;
}

const char *oamr_co_sdo_abort_name(uint32_t abort_code)
{
    switch (abort_code) {
    case OAMR_CO_SDO_ABORT_TOGGLE: return "toggle bit not alternated";
    case OAMR_CO_SDO_ABORT_TIMEOUT: return "SDO protocol timed out";
    case OAMR_CO_SDO_ABORT_BAD_CS: return "command specifier not valid";
    case OAMR_CO_SDO_ABORT_UNSUPPORTED_ACCESS: return "unsupported access";
    case OAMR_CO_SDO_ABORT_WRITE_ONLY: return "read of a write-only object";
    case OAMR_CO_SDO_ABORT_READ_ONLY: return "write of a read-only object";
    case OAMR_CO_SDO_ABORT_NO_OBJECT: return "object does not exist";
    case OAMR_CO_SDO_ABORT_LENGTH: return "data type length mismatch";
    case OAMR_CO_SDO_ABORT_NO_SUBINDEX: return "subindex does not exist";
    case OAMR_CO_SDO_ABORT_VALUE_RANGE: return "value range exceeded";
    case OAMR_CO_SDO_ABORT_GENERAL: return "general error";
    case OAMR_CO_SDO_ABORT_DATA_TRANSFER: return "data cannot be transferred";
    case OAMR_CO_SDO_ABORT_DATA_LOCAL: return "data cannot be transferred (local control)";
    case OAMR_CO_SDO_ABORT_DEVICE_STATE: return "data cannot be transferred (device state)";
    default: return "unknown";
    }
}
