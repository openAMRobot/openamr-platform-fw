/* SPDX-License-Identifier: MIT */
#include "oamr/mono_time.h"

#include <string.h>

bool oamr_mono_ext_init(oamr_mono_ext_t *m, uint8_t width_bits, uint32_t ticks_per_us,
                        uint32_t first_raw)
{
    memset(m, 0, sizeof(*m));
    if (width_bits == 0u || width_bits > 32u || ticks_per_us == 0u)
    {
        return false;
    }
    m->mask = width_bits == 32u ? 0xFFFFFFFFu : (uint32_t)((1u << width_bits) - 1u);
    m->ticks_per_us = ticks_per_us;
    m->last_raw = first_raw & m->mask;
    m->configured = true;
    return true;
}

uint64_t oamr_mono_ext_update(oamr_mono_ext_t *m, uint32_t raw)
{
    if (!m->configured)
    {
        return 0u;
    }
    const uint32_t now = raw & m->mask;
    /* Modular difference within the counter width: correct across one wrap. */
    const uint32_t delta = (uint32_t)(now - m->last_raw) & m->mask;
    m->last_raw = now;
    m->ticks += delta;
    return m->ticks / m->ticks_per_us;
}
