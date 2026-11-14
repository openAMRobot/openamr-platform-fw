/* SPDX-License-Identifier: MIT */
#include "oamr/config_readback.h"

#include <string.h>

static int find_entry(const oamr_co_upl_t *s, uint16_t index, uint8_t subindex)
{
    for (uint8_t i = 0; i < s->cfg.n_objects; i++) {
        if (s->cfg.objects[i].index == index && s->cfg.objects[i].subindex == subindex) {
            return (int)i;
        }
    }
    return -1;
}

void oamr_rb_compare(const oamr_rb_expect_t *expect, uint8_t n_expect, const oamr_co_upl_t *sched,
                     uint64_t max_age_us, uint64_t now_us, oamr_rb_report_t *out)
{
    memset(out, 0, sizeof(*out));
    if (expect == 0 || sched == 0 || n_expect == 0u || n_expect > OAMR_RB_MAX_ENTRIES ||
        max_age_us == 0u || sched->state == OAMR_CO_UPL_NOT_CONFIGURED) {
        return; /* configured false, all_match false */
    }
    out->configured = true;
    out->n = n_expect;
    for (uint8_t i = 0; i < n_expect; i++) {
        const oamr_rb_expect_t *e = &expect[i];
        oamr_rb_entry_report_t *r = &out->entries[i];
        if (e->mask == 0u || (e->size != 1u && e->size != 2u && e->size != 4u)) {
            r->result = OAMR_RB_BAD_EXPECT;
            out->unverified++;
            continue;
        }
        const int k = find_entry(sched, e->index, e->subindex);
        if (k < 0) {
            r->result = OAMR_RB_NOT_IN_LIST;
            out->unverified++;
            continue;
        }
        const oamr_co_sample_t *s = &sched->samples[k];
        if (s->status != OAMR_CO_SAMPLE_VALID || now_us < s->sample_us ||
            now_us - s->sample_us > max_age_us) {
            r->result = OAMR_RB_UNVERIFIED;
            out->unverified++;
            continue;
        }
        r->read_value = s->value;
        r->read_size = s->size;
        if (s->size != e->size) {
            r->result = OAMR_RB_SIZE_MISMATCH;
            out->mismatches++;
        } else if ((s->value & e->mask) != (e->expected & e->mask)) {
            r->result = OAMR_RB_MISMATCH;
            out->mismatches++;
        } else {
            r->result = OAMR_RB_MATCH;
        }
    }
    out->all_match = out->mismatches == 0u && out->unverified == 0u;
}

const char *oamr_rb_result_name(oamr_rb_result_t r)
{
    switch (r) {
    case OAMR_RB_MATCH: return "MATCH";
    case OAMR_RB_MISMATCH: return "MISMATCH";
    case OAMR_RB_SIZE_MISMATCH: return "SIZE_MISMATCH";
    case OAMR_RB_UNVERIFIED: return "UNVERIFIED";
    case OAMR_RB_NOT_IN_LIST: return "NOT_IN_LIST";
    case OAMR_RB_BAD_EXPECT: return "BAD_EXPECT";
    default: return "INVALID";
    }
}
