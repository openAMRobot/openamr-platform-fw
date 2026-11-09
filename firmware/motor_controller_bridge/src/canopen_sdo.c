/* SPDX-License-Identifier: MIT */
#include "oamr/canopen_sdo.h"

#include <string.h>

/* CiA 301 expedited SDO command specifiers. */
#define CCS_DOWNLOAD_4 0x23u
#define CCS_DOWNLOAD_2 0x2Bu
#define CCS_DOWNLOAD_1 0x2Fu
#define CCS_UPLOAD 0x40u
#define SCS_DOWNLOAD_ACK 0x60u
#define SCS_ABORT 0x80u

void oamr_sdo_init(oamr_sdo_client_t *c, uint8_t node_id, uint64_t timeout_us)
{
    memset(c, 0, sizeof(*c));
    c->node_id = node_id;
    c->timeout_us = timeout_us;
    c->state = OAMR_SDO_IDLE;
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void request_frame(const oamr_sdo_client_t *c, uint8_t cmd, uint32_t value, oamr_can_frame_t *f)
{
    memset(f, 0, sizeof(*f));
    f->id = OAMR_SDO_TX_BASE + c->node_id;
    f->dlc = 8;
    f->data[0] = cmd;
    f->data[1] = (uint8_t)(c->index & 0xFFu);
    f->data[2] = (uint8_t)(c->index >> 8);
    f->data[3] = c->subindex;
    put_le32(&f->data[4], value);
}

static bool start(oamr_sdo_client_t *c, const oamr_can_tx_t *tx, uint8_t cmd, uint64_t now_us)
{
    oamr_can_frame_t f;
    request_frame(c, cmd, c->is_read ? 0u : c->value, &f);
    c->abort_code = 0;
    c->started_us = now_us;
    if (!oamr_can_send(tx, &f)) {
        c->state = OAMR_SDO_TX_FAILED;
        return false;
    }
    c->state = OAMR_SDO_BUSY;
    return true;
}

bool oamr_sdo_start_read(oamr_sdo_client_t *c, const oamr_can_tx_t *tx, uint16_t index,
                         uint8_t subindex, uint64_t now_us)
{
    if (c->state == OAMR_SDO_BUSY) {
        return false;
    }
    c->is_read = true;
    c->index = index;
    c->subindex = subindex;
    c->size = 0;
    c->value = 0;
    return start(c, tx, CCS_UPLOAD, now_us);
}

bool oamr_sdo_start_write(oamr_sdo_client_t *c, const oamr_can_tx_t *tx, uint16_t index,
                          uint8_t subindex, uint32_t value, uint8_t size, uint64_t now_us)
{
    uint8_t cmd;
    switch (size) {
    case 1: cmd = CCS_DOWNLOAD_1; value &= 0xFFu; break;
    case 2: cmd = CCS_DOWNLOAD_2; value &= 0xFFFFu; break;
    case 4: cmd = CCS_DOWNLOAD_4; break;
    default: return false;
    }
    if (c->state == OAMR_SDO_BUSY) {
        return false;
    }
    c->is_read = false;
    c->index = index;
    c->subindex = subindex;
    c->size = size;
    c->value = value;
    return start(c, tx, cmd, now_us);
}

bool oamr_sdo_on_frame(oamr_sdo_client_t *c, const oamr_can_frame_t *f)
{
    if (f->extended || f->id != OAMR_SDO_RX_BASE + c->node_id || f->dlc < 8) {
        return false;
    }
    if (c->state != OAMR_SDO_BUSY) {
        return true; /* late answer to a transfer that already timed out */
    }
    const uint16_t index = (uint16_t)(f->data[1] | ((uint16_t)f->data[2] << 8));
    if (index != c->index || f->data[3] != c->subindex) {
        return true; /* not ours: keep waiting */
    }
    const uint8_t scs = f->data[0];
    if (scs == SCS_ABORT) {
        c->abort_code = get_le32(&f->data[4]);
        c->state = OAMR_SDO_ABORTED;
        return true;
    }
    if (!c->is_read) {
        c->state = (scs == SCS_DOWNLOAD_ACK) ? OAMR_SDO_DONE : OAMR_SDO_ABORTED;
        return true;
    }
    /* Expedited upload: 0x4x with e=1 (bit 1). s=1 (bit 0) means n is valid. */
    if ((scs & 0xE0u) != 0x40u || (scs & 0x02u) == 0u) {
        c->state = OAMR_SDO_ABORTED; /* segmented transfer not supported */
        return true;
    }
    uint8_t size = 4;
    if (scs & 0x01u) {
        size = (uint8_t)(4u - ((scs >> 2) & 0x03u));
    }
    uint32_t v = get_le32(&f->data[4]);
    if (size < 4) {
        v &= (1u << (8u * size)) - 1u;
    }
    c->size = size;
    c->value = v;
    c->state = OAMR_SDO_DONE;
    return true;
}

void oamr_sdo_poll(oamr_sdo_client_t *c, const oamr_can_tx_t *tx, uint64_t now_us)
{
    if (c->state != OAMR_SDO_BUSY) {
        return;
    }
    if (now_us - c->started_us >= c->timeout_us) {
        oamr_can_frame_t f;
        request_frame(c, SCS_ABORT, OAMR_SDO_ABORT_TIMEOUT, &f);
        (void)oamr_can_send(tx, &f);
        c->abort_code = OAMR_SDO_ABORT_TIMEOUT;
        c->state = OAMR_SDO_TIMEOUT;
    }
}
