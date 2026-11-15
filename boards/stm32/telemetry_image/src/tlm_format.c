/* SPDX-License-Identifier: MIT */
/* Status line formatter of the telemetry image. Pure C: no HAL, no RTOS, no
 * heap, no printf. */
#include "tlm_status.h"

#include <string.h>

#include "tlm_params.h"

void tlm_buf_init(tlm_buf_t *b, char *storage, size_t cap)
{
    b->buf = storage;
    b->cap = cap;
    b->len = 0u;
    b->overflow = false;
}

static void put(tlm_buf_t *b, char c)
{
    if (b->len < b->cap) {
        b->buf[b->len++] = c;
    } else {
        b->overflow = true;
    }
}

void tlm_buf_str(tlm_buf_t *b, const char *s)
{
    while (*s != '\0') {
        put(b, *s++);
    }
}

void tlm_buf_u64(tlm_buf_t *b, uint64_t v)
{
    char tmp[20];
    unsigned n = 0u;
    do {
        tmp[n++] = (char)('0' + (char)(v % 10u));
        v /= 10u;
    } while (v != 0u);
    while (n > 0u) {
        put(b, tmp[--n]);
    }
}

void tlm_buf_i64(tlm_buf_t *b, int64_t v)
{
    if (v < 0) {
        put(b, '-');
        tlm_buf_u64(b, (uint64_t)0 - (uint64_t)v);
    } else {
        tlm_buf_u64(b, (uint64_t)v);
    }
}

void tlm_buf_hex(tlm_buf_t *b, uint32_t v, unsigned digits)
{
    static const char hex[] = "0123456789abcdef";
    tlm_buf_str(b, "0x");
    while (digits > 0u) {
        digits--;
        put(b, hex[(v >> (4u * digits)) & 0xFu]);
    }
}

void tlm_buf_key(tlm_buf_t *b, const char *key)
{
    put(b, '"');
    tlm_buf_str(b, key);
    tlm_buf_str(b, "\":");
}

static void qstr(tlm_buf_t *b, const char *text)
{
    put(b, '"');
    tlm_buf_str(b, text);
    put(b, '"');
}

void tlm_buf_kv_str(tlm_buf_t *b, const char *key, const char *text)
{
    tlm_buf_key(b, key);
    qstr(b, text);
}

void tlm_buf_kv_u64(tlm_buf_t *b, const char *key, uint64_t v)
{
    tlm_buf_key(b, key);
    tlm_buf_u64(b, v);
}

void tlm_buf_kv_opt(tlm_buf_t *b, const char *key, tlm_opt_u64_t v)
{
    tlm_buf_key(b, key);
    if (v.known) {
        tlm_buf_u64(b, v.v);
    } else {
        qstr(b, "unknown");
    }
}

void tlm_buf_kv_bool(tlm_buf_t *b, const char *key, bool v)
{
    tlm_buf_key(b, key);
    tlm_buf_str(b, v ? "true" : "false");
}

/* ---- disabled features ---- */
typedef struct {
    bool disabled;
    const char *name;
    const char *reason;
} tlm_feature_t;

void tlm_format_disabled(tlm_buf_t *b, bool can_started, bool with_reason)
{
    const tlm_feature_t f[] = {
        {!can_started, "can1", "FDCAN1 did not start"},
        {TLM_HB_TIMEOUT_SET == 0, "hb", "TLM_HB_TIMEOUT_US unset (TODO(Evans), D-24 proposes 300 ms)"},
        {TLM_SDO_TIMEOUT_SET == 0 || TLM_SDO_RETRIES_SET == 0, "sdo",
         "TLM_SDO_TIMEOUT_US or TLM_SDO_RETRIES unset (TODO(Evans)): no upload is sent"},
        {TLM_SDO_HEALTH_N_SET == 0, "sdo_health_n",
         "TLM_SDO_HEALTH_N unset: protocol-error criterion of the SDO health disabled"},
        {TLM_SDO_HEALTH_TIME_SET == 0, "sdo_health_t",
         "TLM_SDO_HEALTH_TIME_US unset: valid-cycle criterion of the SDO health disabled"},
        {TLM_DRIVE_MAX_AGE_SET == 0, "drive_age", "TLM_DRIVE_MAX_AGE_US unset (TODO(Evans))"},
        {TLM_SPEED_RESOLUTION_SET == 0, "speed_res",
         "TLM_SPEED_RESOLUTION_REQUIRED unset: velocity stays unconfirmed"},
        {TLM_TFLUNA_UNIT_SET == 0, "tfluna", "TLM_TFLUNA_UNIT unset: frames are not decoded"},
        {TLM_TFLUNA_STALE_SET == 0, "tfluna_stale", "TLM_TFLUNA_STALE_US unset: validity unknown"},
        {TLM_MB7060_STALE_SET == 0, "mb7060_stale", "TLM_MB7060_STALE_US unset"},
        {TLM_MB7060_TRIGGER_SET == 0, "mb7060_range",
         "no trigger pulse in this image: no range is attributed to a reading"},
        {TLM_LIFT_HB_TIMEOUT_SET == 0, "lift_hb",
         "TLM_LIFT_HB_TIMEOUT_US unset (TODO(Evans), D15 proposes 300 ms)"},
        {TLM_DALY_ADDR_SET == 0, "bms", "Daly addresses unset (TODO(Evans)): CAN2 frames are not decoded"},
        {TLM_DALY_PROTOCOL_CONFIRMED == 0, "bms_values",
         "Daly protocol not confirmed for the delivered unit: battery values locked"},
    };
    bool first = true;
    for (size_t i = 0u; i < sizeof(f) / sizeof(f[0]); i++) {
        if (!f[i].disabled) {
            continue;
        }
        if (!first) {
            put(b, ',');
        }
        first = false;
        put(b, '"');
        tlm_buf_str(b, f[i].name);
        if (with_reason) {
            tlm_buf_str(b, ": ");
            tlm_buf_str(b, f[i].reason);
        }
        put(b, '"');
    }
}

/* ---- status line ---- */
static void obj(tlm_buf_t *b, const char *key, const tlm_obj_t *o)
{
    tlm_buf_key(b, key);
    put(b, '{');
    tlm_buf_kv_str(b, "v", o->validity);
    put(b, ',');
    tlm_buf_key(b, "raw");
    if (!o->raw_known) {
        qstr(b, "unknown");
    } else if (o->hex_digits != 0u) {
        put(b, '"');
        tlm_buf_hex(b, (uint32_t)o->raw, o->hex_digits);
        put(b, '"');
    } else {
        tlm_buf_i64(b, o->raw);
    }
    put(b, ',');
    tlm_buf_kv_opt(b, "age_us", o->age_us);
    put(b, ',');
    tlm_buf_kv_u64(b, "n", o->reads);
    if (o->abort_known) {
        put(b, ',');
        tlm_buf_key(b, "abort");
        put(b, '"');
        tlm_buf_hex(b, o->abort_code, 8u);
        put(b, '"');
    }
    if (o->state != 0) {
        put(b, ',');
        tlm_buf_kv_str(b, "state", o->state);
    }
    put(b, '}');
}

static void bus_off(tlm_buf_t *b, const tlm_drive_status_t *d)
{
    tlm_buf_key(b, "bus_off");
    if (d->bus_state_known) {
        tlm_buf_str(b, d->bus_off ? "true" : "false");
    } else {
        qstr(b, "unknown");
    }
}

/* Fast line: heartbeat and the four values that change every read. */
static void drive(tlm_buf_t *b, const tlm_drive_status_t *d)
{
    tlm_buf_str(b, "\"hb\":{");
    tlm_buf_kv_str(b, "st", d->hb_state);
    put(b, ',');
    tlm_buf_kv_opt(b, "age_us", d->hb_age_us);
    tlm_buf_str(b, "},\"wheel\":{");
    obj(b, "ch1", &d->wheel_ch1);
    put(b, ',');
    obj(b, "ch2", &d->wheel_ch2);
    tlm_buf_str(b, "},");
    obj(b, "sw", &d->statusword);
    put(b, ',');
    obj(b, "fault", &d->fault);
}

static void kv_opt_or_unknown_i64(tlm_buf_t *b, const char *key, bool known, int64_t v)
{
    tlm_buf_key(b, key);
    if (known) {
        tlm_buf_i64(b, v);
    } else {
        qstr(b, "unknown");
    }
}

/* Fast line: the TF-Luna ports. A port whose reception failed says so. */
static void sensors_fast(tlm_buf_t *b, const tlm_sensors_status_t *s)
{
    for (unsigned i = 0u; i < TLM_N_TFLUNA; i++) {
        const tlm_tfl_status_t *t = &s->tfl[i];
        tlm_buf_key(b, t->name);
        put(b, '{');
        tlm_buf_kv_str(b, "st", t->status);
        put(b, ',');
        tlm_buf_key(b, "v");
        if (t->valid_known) {
            tlm_buf_str(b, t->valid ? "true" : "false");
        } else {
            qstr(b, "unknown");
        }
        put(b, ',');
        tlm_buf_key(b, "mm");
        if (t->distance_known) {
            tlm_buf_u64(b, t->distance_mm);
        } else {
            qstr(b, "unknown");
        }
        put(b, ',');
        tlm_buf_kv_opt(b, "age_us", t->age_us);
        put(b, ',');
        tlm_buf_kv_u64(b, "n", t->frames);
        if (s->rx_failed[TLM_N_MB7060 + i]) {
            tlm_buf_str(b, ",\"rx\":\"failed\"");
        }
        put(b, '}');
        if (i + 1u < TLM_N_TFLUNA) {
            put(b, ',');
        }
    }
}

static size_t digits(uint64_t v)
{
    size_t n = 1u;
    while (v >= 10u) {
        v /= 10u;
        n++;
    }
    return n;
}

/* Counters: only the ones that are not zero are printed, and only as many as
 * fit in the room the line has left under its cap (the closing "}}\n" and a
 * possible "cnt_more" field are reserved). Without "cnt_more", a counter
 * that is absent from "cnt" is zero. With "cnt_more":N, N non-zero counters
 * did not fit and an absent counter is unknown for this line. Counters are
 * never "unknown" themselves. Returns the number left out. */
#define CNT_RESERVE (sizeof("},\"cnt_more\":99}\n") - 1u)
static uint32_t counters(tlm_buf_t *b, const tlm_status_t *s)
{
    const tlm_drive_status_t *d = &s->drive;
    uint32_t mb_bad = 0u;
    uint32_t tf_bad = 0u;
    for (unsigned i = 0u; i < TLM_N_MB7060; i++) {
        mb_bad += s->sensors.mb[i].malformed;
    }
    for (unsigned i = 0u; i < TLM_N_TFLUNA; i++) {
        tf_bad += s->sensors.tfl[i].checksum_errors;
    }
    const struct {
        const char *k;
        uint32_t v;
    } c[] = {
        {"rx", d->can_rx},
        {"tx", d->can_tx},
        {"tx_fail", d->can_tx_failed},
        {"tx_ref", d->can_tx_refused},
        {"rx_hw_lost", d->can_rx_hw_lost},
        {"rx_q_ovf", d->can_rx_q_ovf},
        {"rx_defer", d->can_rx_deferred},
        {"tick_miss", d->tick_missed},
        {"sdo_ok", d->sdo_ok},
        {"sdo_to", d->sdo_timeouts},
        {"sdo_ab", d->sdo_aborts},
        {"sdo_pe", d->sdo_protocol},
        {"sdo_un", d->sdo_unsolicited},
        {"sdo_wo", d->sdo_wrong_object},
        {"sdo_late", d->sdo_late},
        {"hb", d->hb_count},
        {"hb_boot", d->hb_bootups},
        {"hb_lost", d->hb_lost_events},
        {"hb_bad", d->hb_malformed},
        {"mb_bad", mb_bad},
        {"tf_bad", tf_bad},
        {"uart_err", s->sensors.uart_errors},
        {"ring_ovf", s->sensors.ring_overflows},
        {"ring_lost", s->sensors.ring_lost_bytes},
        {"rx_start_fail", s->sensors.rx_start_failures},
        {"rx_restart", s->sensors.rx_restarts},
        {"c2_rx", s->aux.c2_rx},
        {"c3_rx", s->aux.c3_rx},
        {"c2_lost", s->aux.c2_lost},
        {"c3_lost", s->aux.c3_lost},
        {"c2_tx_ref", s->aux.c2_tx_ref},
        {"c3_tx_ref", s->aux.c3_tx_ref},
        {"lift_hb", s->aux.lift_hb_n},
        {"lift_hb_lost", s->aux.lift_hb_lost},
        {"lift_gap", s->aux.lift_gaps},
        {"lift_bad", s->aux.lift_bad},
        {"lift_emcy", s->aux.lift_emcy},
        {"overrun", s->report_overruns},
        {"dropped", s->lines_dropped},
        {"too_long", s->lines_too_long},
    };
    bool first = true;
    uint32_t left_out = 0u;
    for (size_t i = 0u; i < sizeof(c) / sizeof(c[0]); i++) {
        if (c[i].v == 0u) {
            continue;
        }
        /* ,"key":value */
        const size_t need = (first ? 0u : 1u) + strlen(c[i].k) + 3u + digits(c[i].v);
        if (b->overflow || b->len + need + CNT_RESERVE > b->cap) {
            left_out++;
            continue;
        }
        if (!first) {
            put(b, ',');
        }
        first = false;
        tlm_buf_kv_u64(b, c[i].k, c[i].v);
    }
    return left_out;
}

static void header(tlm_buf_t *b, const char *type, const tlm_status_t *s)
{
    tlm_buf_str(b, "{\"t\":\"");
    tlm_buf_str(b, type);
    tlm_buf_str(b, "\",");
    tlm_buf_kv_u64(b, "boot", s->boot.id);
    put(b, ',');
    tlm_buf_kv_u64(b, "seq", s->seq);
}

bool tlm_format_status(const tlm_status_t *s, tlm_buf_t *b)
{
    header(b, "st", s);
    put(b, ',');
    tlm_buf_kv_u64(b, "up_us", s->uptime_us);
    tlm_buf_str(b, ",\"drive\":{");
    drive(b, &s->drive);
    tlm_buf_str(b, "},\"sens\":{");
    sensors_fast(b, &s->sensors);
    tlm_buf_str(b, "},\"cnt\":{");
    const uint32_t left_out = counters(b, s);
    put(b, '}');
    if (left_out != 0u) {
        put(b, ',');
        tlm_buf_kv_u64(b, "cnt_more", left_out);
    }
    tlm_buf_str(b, "}\n");
    return !b->overflow;
}

/* MB7060 port in the slow line: [age_us, frames]. The range is never known
 * in this image (no trigger), so only the arrival of frames is reported. */
static void mb_slow(tlm_buf_t *b, const tlm_mb_status_t *m)
{
    tlm_buf_key(b, m->name);
    put(b, '[');
    if (m->age_us.known) {
        tlm_buf_u64(b, m->age_us.v);
    } else {
        qstr(b, "unknown");
    }
    put(b, ',');
    tlm_buf_u64(b, m->frames);
    put(b, ']');
}

tlm_slow_part_t tlm_slow_part(uint32_t seq)
{
    return ((seq / TLM_SLOW_EVERY) % 2u) == 0u ? TLM_SLOW_CFG : TLM_SLOW_DIAG;
}

bool tlm_format_slow(const tlm_status_t *s, tlm_slow_part_t part, tlm_buf_t *b)
{
    const tlm_drive_status_t *d = &s->drive;
    header(b, "sl", s);
    if (part == TLM_SLOW_CFG) {
        tlm_buf_str(b, ",\"part\":\"cfg\",");
        tlm_buf_kv_str(b, "params", s->params);
        put(b, ',');
        tlm_buf_kv_str(b, "sdo", d->sdo_enabled ? "on" : "disabled");
        put(b, ',');
        tlm_buf_kv_str(b, "health", d->sdo_health);
        put(b, ',');
        tlm_buf_kv_str(b, "nmt", d->nmt);
        put(b, ',');
        bus_off(b, d);
        put(b, ',');
        obj(b, "res", &d->resolution);
        tlm_buf_str(b, ",\"dis\":[");
        tlm_format_disabled(b, d->can_started, false);
        tlm_buf_str(b, "]}\n");
        return !b->overflow;
    }
    tlm_buf_str(b, ",\"part\":\"diag\",\"tick\":[");
    tlm_buf_u64(b, d->tick_runs);
    put(b, ',');
    tlm_buf_u64(b, d->tick_late_max_ms);
    tlm_buf_str(b, "],\"mb\":{");
    for (unsigned i = 0u; i < TLM_N_MB7060; i++) {
        if (i != 0u) {
            put(b, ',');
        }
        mb_slow(b, &s->sensors.mb[i]);
    }
    /* Only ports that are not receiving are listed. */
    tlm_buf_str(b, "},\"rx\":{");
    bool first = true;
    for (unsigned i = 0u; i < TLM_N_MB7060 + TLM_N_TFLUNA; i++) {
        if (s->sensors.rx_state[i] == 0 || s->sensors.rx_state[i][0] == 'o') { /* "ok" */
            continue;
        }
        if (!first) {
            put(b, ',');
        }
        first = false;
        tlm_buf_kv_str(b, s->sensors.port_name[i], s->sensors.rx_state[i]);
    }
    tlm_buf_str(b, "},\"stk\":{");
    for (unsigned i = 0u; i < TLM_N_TASKS; i++) {
        if (i != 0u) {
            put(b, ',');
        }
        tlm_buf_kv_u64(b, s->stacks.name[i] != 0 ? s->stacks.name[i] : "?", s->stacks.free_words[i]);
    }
    tlm_buf_str(b, "}}\n");
    return !b->overflow;
}

bool tlm_aux_due(uint32_t seq)
{
    return seq % TLM_SLOW_EVERY != 0u;
}

/* "ax" line: CTL-017 lift (CAN3) and Daly BMS (CAN2), receive only. A value
 * that was not received is "unknown"; battery values appear only when the
 * decoder is usable, a locked decoder prints "locked" and no value. */
bool tlm_format_aux(const tlm_status_t *s, tlm_buf_t *b)
{
    const tlm_aux_status_t *a = &s->aux;
    header(b, "ax", s);
    put(b, ',');
    tlm_buf_kv_str(b, "c2", a->can2_started ? "on" : "off");
    put(b, ',');
    tlm_buf_kv_str(b, "c3", a->can3_started ? "on" : "off");
    tlm_buf_str(b, ",\"lift\":{");
    tlm_buf_kv_str(b, "hb", a->lift_hb);
    put(b, ',');
    tlm_buf_kv_opt(b, "hb_age_us", a->lift_hb_age_us);
    put(b, ',');
    tlm_buf_kv_str(b, "nmt", a->lift_nmt);
    put(b, ',');
    tlm_buf_kv_str(b, "st", a->lift_state);
    put(b, ',');
    tlm_buf_kv_opt(b, "pos", a->lift_pos);
    put(b, ',');
    tlm_buf_kv_opt(b, "age_us", a->lift_age_us);
    put(b, ',');
    tlm_buf_key(b, "fl");
    if (a->lift_flags_known) {
        put(b, '"');
        tlm_buf_hex(b, a->lift_flags, 2u);
        put(b, '"');
    } else {
        qstr(b, "unknown");
    }
    put(b, ',');
    kv_opt_or_unknown_i64(b, "vel", a->lift_s2_known, a->lift_vel);
    put(b, ',');
    kv_opt_or_unknown_i64(b, "ma", a->lift_s2_known, a->lift_ma);
    put(b, ',');
    tlm_buf_key(b, "flt");
    if (a->lift_s2_known) {
        put(b, '"');
        tlm_buf_hex(b, a->lift_faults, 3u);
        put(b, '"');
    } else {
        qstr(b, "unknown");
    }
    tlm_buf_str(b, "},");
    if (a->bms_values_known) {
        tlm_buf_str(b, "\"bms\":{");
        tlm_buf_kv_u64(b, "mv", a->bms_mv);
        put(b, ',');
        kv_opt_or_unknown_i64(b, "da", true, a->bms_da);
        put(b, ',');
        tlm_buf_kv_u64(b, "soc_pm", a->bms_soc_pm);
        put(b, ',');
        tlm_buf_kv_opt(b, "age_us", a->bms_age_us);
        put(b, '}');
    } else {
        tlm_buf_kv_str(b, "bms", (a->bms != 0 && a->bms[0] == 'u') ? "unknown" : a->bms);
    }
    /* decoder counters: [decoded, foreign, bad, unknown id] */
    tlm_buf_str(b, ",\"bmsc\":[");
    tlm_buf_u64(b, a->bms_dec);
    put(b, ',');
    tlm_buf_u64(b, a->bms_foreign);
    put(b, ',');
    tlm_buf_u64(b, a->bms_bad);
    put(b, ',');
    tlm_buf_u64(b, a->bms_unknown);
    tlm_buf_str(b, "]}\n");
    return !b->overflow;
}
