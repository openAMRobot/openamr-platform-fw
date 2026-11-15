/* SPDX-License-Identifier: MIT */
/* SDO expedited download encoder; see oamr/co_sdo_download.h. Not part of the
 * read-only library. */
#include "oamr/co_sdo_download.h"

#include <string.h>

#include "oamr/co_cobid.h"

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
    memset(out, 0, sizeof(*out));
    out->id = OAMR_CO_COB_SDO_RX_BASE + node_id;
    out->extended = false;
    out->dlc = 8u;
    out->data[0] = cs;
    out->data[1] = (uint8_t)(index & 0xFFu);
    out->data[2] = (uint8_t)(index >> 8);
    out->data[3] = subindex;
    oamr_co_put_le32(&out->data[4], value);
    return true;
}
