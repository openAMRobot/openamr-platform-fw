/* SPDX-License-Identifier: MIT */
#include "oamr/freshness.h"

#include <string.h>

bool oamr_fresh_init(oamr_fresh_t *f, const oamr_fresh_config_t *cfg)
{
    memset(f, 0, sizeof(*f));
    if (cfg == NULL || cfg->stale_after_us == 0u || cfg->stale_after_us > OAMR_FRESH_MAX_LIMIT_US) {
        return false;
    }
    f->cfg = *cfg;
    f->configured = true;
    return true;
}

void oamr_fresh_reset(oamr_fresh_t *f)
{
    f->has_sample = false;
    f->expired = false;
    f->stamp_us = 0u;
}

void oamr_fresh_mark(oamr_fresh_t *f, uint32_t stamp_us)
{
    if (!f->configured) {
        return;
    }
    f->has_sample = true;
    f->expired = false;
    f->stamp_us = stamp_us;
    f->samples++;
}

oamr_fresh_state_t oamr_fresh_classify(oamr_fresh_t *f, uint32_t now_us)
{
    if (!f->configured) {
        return OAMR_FRESH_UNKNOWN;
    }
    if (!f->has_sample) {
        return OAMR_FRESH_NEVER;
    }
    if (f->expired) {
        return OAMR_FRESH_STALE;
    }
    const uint32_t age = oamr_age_us(now_us, f->stamp_us);
    if (age > OAMR_FRESH_MAX_LIMIT_US || age >= f->cfg.stale_after_us) {
        f->expired = true;
        return OAMR_FRESH_STALE;
    }
    return OAMR_FRESH_FRESH;
}

bool oamr_fresh_age(const oamr_fresh_t *f, uint32_t now_us, uint32_t *age_us)
{
    if (!f->configured || !f->has_sample) {
        return false;
    }
    const uint32_t age = oamr_age_us(now_us, f->stamp_us);
    if (age > OAMR_FRESH_MAX_LIMIT_US) {
        return false;
    }
    *age_us = age;
    return true;
}
