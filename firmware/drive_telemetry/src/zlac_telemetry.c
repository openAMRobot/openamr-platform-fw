/* SPDX-License-Identifier: MIT */
#include "oamr/zlac_telemetry.h"

#include <string.h>

/* Common gate: configuration, sample status, size, age. Returns VALID or the
 * reason the sample cannot be used. expected_size 0 = any of 1, 2, 4. */
static oamr_tlm_validity_t gate(const oamr_tlm_config_t *cfg, const oamr_co_sample_t *s,
                                uint8_t expected_size, uint32_t last_seq, uint64_t now_us,
                                oamr_tlm_meta_t *meta)
{
    memset(meta, 0, sizeof(*meta));
    if (cfg == 0 || cfg->max_age_us == 0u) {
        return meta->validity = OAMR_TLM_NOT_CONFIGURED;
    }
    if (s == 0 || s->status == OAMR_CO_SAMPLE_NONE) {
        return meta->validity = OAMR_TLM_NOT_READ;
    }
    if (s->status != OAMR_CO_SAMPLE_VALID) {
        return meta->validity = OAMR_TLM_READ_FAILED;
    }
    meta->seq = s->seq;
    meta->fresh_seq = s->seq != last_seq;
    if (expected_size != 0u) {
        if (s->size != expected_size) {
            return meta->validity = OAMR_TLM_BAD_SIZE;
        }
        /* A short type must state its size; s = 0 means "4 or unknown". */
        if (expected_size < 4u && !s->size_indicated) {
            return meta->validity = OAMR_TLM_BAD_SIZE;
        }
    }
    if (now_us < s->sample_us || now_us - s->sample_us > cfg->max_age_us) {
        return meta->validity = OAMR_TLM_STALE;
    }
    return meta->validity = OAMR_TLM_VALID;
}

void oamr_tlm_decode_resolution(const oamr_tlm_config_t *cfg, const oamr_co_sample_t *sample,
                                uint32_t required_value, bool required_set, uint32_t last_seq,
                                uint64_t now_us, oamr_tlm_resolution_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!required_set) {
        out->meta.validity = OAMR_TLM_NOT_CONFIGURED;
        return;
    }
    /* Size of 2026h:05 is not documented for V4.2: TODO(Evans) from the EDS. */
    if (gate(cfg, sample, 0u, last_seq, now_us, &out->meta) != OAMR_TLM_VALID) {
        return;
    }
    out->raw = sample->value;
    out->confirmed = sample->value == required_value;
}

void oamr_tlm_decode_velocity(const oamr_tlm_config_t *cfg, const oamr_co_sample_t *sample,
                              const oamr_tlm_resolution_t *resolution, uint32_t last_seq,
                              uint64_t now_us, oamr_tlm_velocity_t *out)
{
    memset(out, 0, sizeof(*out));
    if (gate(cfg, sample, 4u, last_seq, now_us, &out->meta) != OAMR_TLM_VALID) {
        return;
    }
    out->raw = (int32_t)sample->value;
    if (resolution == 0 || resolution->meta.validity != OAMR_TLM_VALID || !resolution->confirmed) {
        out->meta.validity = OAMR_TLM_UNCONFIRMED; /* raw kept for logs; no unit maths */
    }
}

void oamr_tlm_decode_velocity_pair(const oamr_tlm_config_t *cfg, const oamr_co_sample_t *s1,
                                   const oamr_co_sample_t *s2, const oamr_tlm_resolution_t *resolution,
                                   uint32_t last_seq1, uint32_t last_seq2, uint64_t now_us,
                                   oamr_tlm_velocity_pair_t *out)
{
    oamr_tlm_decode_velocity(cfg, s1, resolution, last_seq1, now_us, &out->ch1);
    oamr_tlm_decode_velocity(cfg, s2, resolution, last_seq2, now_us, &out->ch2);
    out->both_valid = out->ch1.meta.validity == OAMR_TLM_VALID && out->ch1.meta.fresh_seq &&
                      out->ch2.meta.validity == OAMR_TLM_VALID && out->ch2.meta.fresh_seq;
}

void oamr_tlm_decode_fault(const oamr_tlm_config_t *cfg, const oamr_co_sample_t *sample,
                           uint32_t last_seq, uint64_t now_us, oamr_tlm_fault_t *out)
{
    memset(out, 0, sizeof(*out));
    if (gate(cfg, sample, 4u, last_seq, now_us, &out->meta) != OAMR_TLM_VALID) {
        return;
    }
    out->raw = sample->value;
    out->half_a = (uint16_t)(sample->value >> 16);
    out->half_b = (uint16_t)(sample->value & 0xFFFFu);
    out->any_fault = sample->value != 0u;
}

oamr_402_state_t oamr_402_state_from_statusword(uint16_t sw)
{
    /* CiA 402 statusword state table. */
    const uint16_t m4f = (uint16_t)(sw & 0x004Fu);
    const uint16_t m6f = (uint16_t)(sw & 0x006Fu);
    if (m4f == 0x0000u) return OAMR_402_NOT_READY_TO_SWITCH_ON;
    if (m4f == 0x0040u) return OAMR_402_SWITCH_ON_DISABLED;
    if (m6f == 0x0021u) return OAMR_402_READY_TO_SWITCH_ON;
    if (m6f == 0x0023u) return OAMR_402_SWITCHED_ON;
    if (m6f == 0x0027u) return OAMR_402_OPERATION_ENABLED;
    if (m6f == 0x0007u) return OAMR_402_QUICK_STOP_ACTIVE;
    if (m4f == 0x000Fu) return OAMR_402_FAULT_REACTION_ACTIVE;
    if (m4f == 0x0008u) return OAMR_402_FAULT;
    return OAMR_402_UNKNOWN;
}

const char *oamr_402_state_name(oamr_402_state_t s)
{
    switch (s) {
    case OAMR_402_NOT_READY_TO_SWITCH_ON: return "NOT_READY_TO_SWITCH_ON";
    case OAMR_402_SWITCH_ON_DISABLED: return "SWITCH_ON_DISABLED";
    case OAMR_402_READY_TO_SWITCH_ON: return "READY_TO_SWITCH_ON";
    case OAMR_402_SWITCHED_ON: return "SWITCHED_ON";
    case OAMR_402_OPERATION_ENABLED: return "OPERATION_ENABLED";
    case OAMR_402_QUICK_STOP_ACTIVE: return "QUICK_STOP_ACTIVE";
    case OAMR_402_FAULT_REACTION_ACTIVE: return "FAULT_REACTION_ACTIVE";
    case OAMR_402_FAULT: return "FAULT";
    case OAMR_402_UNKNOWN:
    default: return "UNKNOWN";
    }
}

void oamr_tlm_decode_statusword(const oamr_tlm_config_t *cfg, const oamr_co_sample_t *sample,
                                uint32_t last_seq, uint64_t now_us, oamr_tlm_statusword_t *out)
{
    memset(out, 0, sizeof(*out));
    if (gate(cfg, sample, 2u, last_seq, now_us, &out->meta) != OAMR_TLM_VALID) {
        return;
    }
    const uint16_t sw = (uint16_t)(sample->value & 0xFFFFu);
    out->raw = sw;
    out->state = oamr_402_state_from_statusword(sw);
    out->fault = (sw & OAMR_SW_FAULT) != 0u;
    out->warning = (sw & OAMR_SW_WARNING) != 0u;
    out->voltage_enabled = (sw & OAMR_SW_VOLTAGE_ENABLED) != 0u;
    out->quick_stop_active = (sw & OAMR_SW_QUICK_STOP) == 0u;
    out->remote = (sw & OAMR_SW_REMOTE) != 0u;
    out->target_reached = (sw & OAMR_SW_TARGET_REACHED) != 0u;
    out->internal_limit = (sw & OAMR_SW_INTERNAL_LIMIT) != 0u;
}

const char *oamr_tlm_validity_name(oamr_tlm_validity_t v)
{
    switch (v) {
    case OAMR_TLM_NOT_CONFIGURED: return "NOT_CONFIGURED";
    case OAMR_TLM_VALID: return "VALID";
    case OAMR_TLM_NOT_READ: return "NOT_READ";
    case OAMR_TLM_READ_FAILED: return "READ_FAILED";
    case OAMR_TLM_BAD_SIZE: return "BAD_SIZE";
    case OAMR_TLM_STALE: return "STALE";
    case OAMR_TLM_UNCONFIRMED: return "UNCONFIRMED";
    default: return "INVALID";
    }
}
