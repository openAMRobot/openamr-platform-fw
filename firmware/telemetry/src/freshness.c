/* SPDX-License-Identifier: MIT */
#include "oamr/freshness.h"

#include <string.h>

bool oamr_fresh_init(oamr_fresh_t *f, const oamr_fresh_config_t *cfg)
{
    memset(f, 0, sizeof(*f));
    if (cfg == NULL || cfg->stale_after_us == 0u)
    {
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

void oamr_fresh_mark(oamr_fresh_t *f, uint64_t stamp_us)
{
    if (!f->configured)
    {
        return;
    }
    f->has_sample = true;
    f->expired = false;
    f->stamp_us = stamp_us;
    f->samples++;
}

oamr_fresh_state_t oamr_fresh_classify(oamr_fresh_t *f, uint64_t now_us)
{
    if (!f->configured)
    {
        return OAMR_FRESH_UNKNOWN;
    }
    if (!f->has_sample)
    {
        return OAMR_FRESH_NEVER;
    }
    if (f->expired)
    {
        return OAMR_FRESH_STALE;
    }
    if (now_us < f->stamp_us || now_us - f->stamp_us >= f->cfg.stale_after_us)
    {
        f->expired = true;
        return OAMR_FRESH_STALE;
    }
    return OAMR_FRESH_FRESH;
}

bool oamr_fresh_age(const oamr_fresh_t *f, uint64_t now_us, uint64_t *age_us)
{
    if (!f->configured || !f->has_sample || now_us < f->stamp_us)
    {
        return false;
    }
    *age_us = now_us - f->stamp_us;
    return true;
}
