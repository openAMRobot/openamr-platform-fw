/* SPDX-License-Identifier: MIT */
#include "tlm_uros_msg.h"

#include <math.h>
#include <string.h>

static float wheel(const tlm_obj_t *o)
{
    if (o->validity != 0 && strcmp(o->validity, "valid") == 0 && o->raw_known)
    {
        return (float)o->raw;
    }
    return NAN;
}

static float range_m(const tlm_tfl_status_t *t)
{
    if (t->valid_known && t->valid && t->distance_known)
    {
        return (float)t->distance_mm / 1000.0f;
    }
    return NAN;
}

static void kv_opt(tlm_buf_t *b, const char *k, tlm_opt_u64_t v)
{
    tlm_buf_kv_opt(b, k, v);
}

void tlm_uros_values(const tlm_status_t *s, const tlm_uros_meta_t *meta, tlm_uros_values_t *out)
{
    out->wheel[0] = wheel(&s->drive.wheel_ch1);
    out->wheel[1] = wheel(&s->drive.wheel_ch2);
    out->drive_hb = s->drive.hb_state != 0 ? s->drive.hb_state : "unknown";
    for (unsigned i = 0u; i < TLM_N_TFLUNA; i++)
    {
        out->range_m[i] = range_m(&s->sensors.tfl[i]);
    }
    out->stamp_sec = (uint32_t)(s->uptime_us / 1000000u);
    out->stamp_nanosec = (uint32_t)((s->uptime_us % 1000000u) * 1000u);

    tlm_buf_t b;
    tlm_buf_init(&b, out->status, sizeof(out->status) - 1u);
    tlm_buf_str(&b, "{");
    tlm_buf_kv_u64(&b, "boot", s->boot.id);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_u64(&b, "seq", s->seq);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_u64(&b, "up_us", s->uptime_us);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_str(&b, "commit", meta->commit);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_str(&b, "params", s->params != 0 ? s->params : "unknown");
    tlm_buf_str(&b, ",");
    tlm_buf_kv_str(&b, "uros", meta->client_state);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_u64(&b, "sessions", meta->sessions);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_u64(&b, "losses", meta->losses);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_u64(&b, "tick_miss", s->drive.tick_missed);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_u64(&b, "sens_miss", meta->sens_missed);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_u64(&b, "arena", meta->arena_high_water);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_u64(&b, "stk", meta->stack_free_words);
    tlm_buf_str(&b, ",\"lift\":{");
    tlm_buf_kv_str(&b, "hb", s->aux.lift_hb != 0 ? s->aux.lift_hb : "unknown");
    tlm_buf_str(&b, ",");
    tlm_buf_kv_str(&b, "st", s->aux.lift_state != 0 ? s->aux.lift_state : "unknown");
    tlm_buf_str(&b, ",");
    kv_opt(&b, "pos", s->aux.lift_pos);
    tlm_buf_str(&b, ",");
    kv_opt(&b, "age_us", s->aux.lift_age_us);
    tlm_buf_str(&b, "},");
    /* Battery: only "locked", "disabled" or "unknown" unless the decoder is usable. */
    if (s->aux.bms_values_known)
    {
        tlm_buf_str(&b, "\"bms\":{");
        tlm_buf_kv_u64(&b, "mv", s->aux.bms_mv);
        tlm_buf_str(&b, ",\"da\":");
        tlm_buf_i64(&b, s->aux.bms_da);
        tlm_buf_str(&b, ",");
        tlm_buf_kv_u64(&b, "soc_pm", s->aux.bms_soc_pm);
        tlm_buf_str(&b, "}");
    }
    else
    {
        const char *bms = s->aux.bms != 0 ? s->aux.bms : "unknown";
        tlm_buf_kv_str(&b, "bms", bms[0] == 'u' ? "unknown" : bms);
    }
    tlm_buf_str(&b, "}");
    if (b.overflow)
    {
        out->status_len = 0u;
        out->status[0] = '\0';
        return;
    }
    out->status[b.len] = '\0';
    out->status_len = b.len;
}
