/* SPDX-License-Identifier: MIT */
#include "oamr/tfluna.h"

#include <string.h>

bool oamr_tfl_init(oamr_tfl_t *p, const oamr_tfl_config_t *cfg)
{
    memset(p, 0, sizeof(*p));
    if (cfg == NULL || (cfg->unit != OAMR_TFL_UNIT_CM && cfg->unit != OAMR_TFL_UNIT_MM)) {
        return false;
    }
    p->cfg = *cfg;
    p->configured = true;
    return true;
}

void oamr_tfl_flush(oamr_tfl_t *p)
{
    p->counters.bytes_discarded += p->len;
    p->len = 0u;
}

static uint8_t checksum(const uint8_t *f)
{
    uint32_t sum = 0u;
    for (unsigned i = 0u; i < OAMR_TFL_FRAME_LEN - 1u; i++) {
        sum += f[i];
    }
    return (uint8_t)(sum & 0xFFu);
}

static oamr_tfl_status_t classify(uint16_t dist_raw, uint32_t dist_mm, uint16_t amp)
{
    if (amp == OAMR_TFL_AMP_OVEREXPOSED || amp > OAMR_TFL_AMP_AMBIENT_LIMIT) {
        return OAMR_TFL_OVEREXPOSED;
    }
    if (dist_raw == 0u) {
        return OAMR_TFL_NO_RETURN;
    }
    if (amp < OAMR_TFL_AMP_MIN_RELIABLE) {
        return OAMR_TFL_WEAK;
    }
    if (dist_mm < OAMR_TFL_BLIND_ZONE_MM) {
        return OAMR_TFL_BELOW_MIN;
    }
    if (dist_mm > OAMR_TFL_MAX_RELIABLE_MM) {
        return OAMR_TFL_ABOVE_MAX;
    }
    return OAMR_TFL_OK;
}

bool oamr_tfl_decode(const uint8_t frame[OAMR_TFL_FRAME_LEN], oamr_tfl_unit_t unit,
                     oamr_tfl_sample_t *out)
{
    if (frame[0] != OAMR_TFL_HEADER || frame[1] != OAMR_TFL_HEADER ||
        checksum(frame) != frame[OAMR_TFL_FRAME_LEN - 1u]) {
        return false;
    }
    if (unit != OAMR_TFL_UNIT_CM && unit != OAMR_TFL_UNIT_MM) {
        return false;
    }
    const uint16_t dist = (uint16_t)(frame[2] | ((uint16_t)frame[3] << 8));
    const uint16_t amp = (uint16_t)(frame[4] | ((uint16_t)frame[5] << 8));
    const uint16_t temp = (uint16_t)(frame[6] | ((uint16_t)frame[7] << 8));
    const uint32_t dist_mm = (unit == OAMR_TFL_UNIT_CM) ? (uint32_t)dist * 10u : (uint32_t)dist;

    memset(out, 0, sizeof(*out));
    out->raw_distance = dist;
    out->distance_mm = dist_mm;
    out->strength = amp;
    out->temperature_c = (float)temp / 8.0f - 256.0f;
    out->status = classify(dist, dist_mm, amp);
    out->valid = (out->status == OAMR_TFL_OK);
    return true;
}

/* After a rejected frame, keep the bytes from the next possible header. */
static void resync(oamr_tfl_t *p)
{
    uint8_t start = 1u;
    while (start < p->len) {
        if (p->buf[start] == OAMR_TFL_HEADER &&
            (start + 1u >= p->len || p->buf[start + 1u] == OAMR_TFL_HEADER)) {
            break;
        }
        start++;
    }
    p->counters.bytes_discarded += start;
    p->len = (uint8_t)(p->len - start);
    memmove(p->buf, &p->buf[start], p->len);
}

oamr_tfl_push_t oamr_tfl_push(oamr_tfl_t *p, uint8_t byte, oamr_tfl_sample_t *out)
{
    if (!p->configured) {
        return OAMR_TFL_PUSH_NOT_CONFIGURED;
    }
    if (p->len == 0u && byte != OAMR_TFL_HEADER) {
        p->counters.bytes_discarded++;
        return OAMR_TFL_PUSH_NONE;
    }
    if (p->len == 1u && byte != OAMR_TFL_HEADER) {
        /* "0x59 x": drop the first byte, and x too since it is not a header. */
        p->counters.bytes_discarded += 2u;
        p->len = 0u;
        return OAMR_TFL_PUSH_NONE;
    }
    p->buf[p->len++] = byte;
    if (p->len < OAMR_TFL_FRAME_LEN) {
        return OAMR_TFL_PUSH_NONE;
    }
    oamr_tfl_sample_t s;
    if (!oamr_tfl_decode(p->buf, p->cfg.unit, &s)) {
        p->counters.checksum_errors++;
        resync(p);
        return OAMR_TFL_PUSH_CHECKSUM_ERROR;
    }
    p->len = 0u;
    p->counters.frames_ok++;
    s.seq = ++p->seq;
    *out = s;
    return OAMR_TFL_PUSH_SAMPLE;
}
