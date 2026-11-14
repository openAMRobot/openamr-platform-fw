/* SPDX-License-Identifier: MIT */
/* Status line formatter of the telemetry image. Pure C: no HAL, no RTOS, no
 * heap, no printf. */
#include "tlm_status.h"

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
        {TLM_DRIVE_MAX_AGE_SET == 0, "drive_age", "TLM_DRIVE_MAX_AGE_US unset (TODO(Evans))"},
        {TLM_SPEED_RESOLUTION_SET == 0, "speed_res",
         "TLM_SPEED_RESOLUTION_REQUIRED unset: velocity stays unconfirmed"},
        {TLM_TFLUNA_UNIT_SET == 0, "tfluna", "TLM_TFLUNA_UNIT unset: frames are not decoded"},
        {TLM_TFLUNA_STALE_SET == 0, "tfluna_stale", "TLM_TFLUNA_STALE_US unset: validity unknown"},
        {TLM_MB7060_STALE_SET == 0, "mb7060_stale", "TLM_MB7060_STALE_US unset"},
        {TLM_MB7060_TRIGGER_SET == 0, "mb7060_range",
         "no trigger pulse in this image: no range is attributed to a reading"},
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

static void drive(tlm_buf_t *b, const tlm_drive_status_t *d)
{
    tlm_buf_str(b, "\"hb\":{");
    tlm_buf_kv_str(b, "st", d->hb_state);
    put(b, ',');
    tlm_buf_kv_opt(b, "age_us", d->hb_age_us);
    put(b, ',');
    tlm_buf_kv_str(b, "nmt", d->nmt);
    tlm_buf_str(b, "},");
    tlm_buf_kv_str(b, "sdo", d->sdo_enabled ? "on" : "disabled");
    put(b, ',');
    tlm_buf_key(b, "bus_off");
    if (d->bus_state_known) {
        tlm_buf_str(b, d->bus_off ? "true" : "false");
    } else {
        qstr(b, "unknown");
    }
    tlm_buf_str(b, ",\"wheel\":{");
    obj(b, "ch1", &d->wheel_ch1);
    put(b, ',');
    obj(b, "ch2", &d->wheel_ch2);
    tlm_buf_str(b, "},");
    obj(b, "sw", &d->statusword);
    put(b, ',');
    obj(b, "fault", &d->fault);
    put(b, ',');
    obj(b, "res", &d->resolution);
}

static void sensors(tlm_buf_t *b, const tlm_sensors_status_t *s)
{
    for (unsigned i = 0u; i < TLM_N_MB7060; i++) {
        const tlm_mb_status_t *m = &s->mb[i];
        tlm_buf_key(b, m->name);
        /* Never armed (no trigger in this image): the range is always unknown. */
        tlm_buf_str(b, "{\"v\":\"unknown\",");
        tlm_buf_kv_opt(b, "age_us", m->age_us);
        put(b, ',');
        tlm_buf_kv_u64(b, "n", m->frames);
        tlm_buf_str(b, "},");
    }
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
        put(b, '}');
        if (i + 1u < TLM_N_TFLUNA) {
            put(b, ',');
        }
    }
}

/* Counters: only the ones that are not zero are printed. A counter that is
 * absent from "cnt" is zero; this keeps the line inside what 115200 baud can
 * carry in one report period. Counters are never "unknown". */
static void counters(tlm_buf_t *b, const tlm_status_t *s)
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
        {"rx", d->can_rx},              {"tx", d->can_tx},
        {"tx_fail", d->can_tx_failed},  {"tx_ref", d->can_tx_refused},
        {"rx_lost", d->can_rx_lost},    {"sdo_ok", d->sdo_ok},
        {"sdo_to", d->sdo_timeouts},    {"sdo_ab", d->sdo_aborts},
        {"sdo_pe", d->sdo_protocol},    {"sdo_un", d->sdo_unsolicited},
        {"sdo_wo", d->sdo_wrong_object}, {"hb", d->hb_count},
        {"hb_boot", d->hb_bootups},     {"hb_lost", d->hb_lost_events},
        {"hb_bad", d->hb_malformed},    {"mb_bad", mb_bad},
        {"tf_bad", tf_bad},             {"uart_err", s->sensors.uart_errors},
        {"ring_ovf", s->sensors.ring_overflows}, {"overrun", s->report_overruns},
        {"dropped", s->lines_dropped},
    };
    bool first = true;
    for (size_t i = 0u; i < sizeof(c) / sizeof(c[0]); i++) {
        if (c[i].v == 0u) {
            continue;
        }
        if (!first) {
            put(b, ',');
        }
        first = false;
        tlm_buf_kv_u64(b, c[i].k, c[i].v);
    }
}

bool tlm_format_status(const tlm_status_t *s, tlm_buf_t *b)
{
    tlm_buf_str(b, "{\"t\":\"st\",");
    tlm_buf_kv_u64(b, "boot", s->boot.id);
    put(b, ',');
    tlm_buf_kv_u64(b, "seq", s->seq);
    put(b, ',');
    tlm_buf_kv_u64(b, "up_us", s->uptime_us);
    put(b, ',');
    tlm_buf_kv_str(b, "params", s->params);
    tlm_buf_str(b, ",\"drive\":{");
    drive(b, &s->drive);
    tlm_buf_str(b, "},\"sens\":{");
    sensors(b, &s->sensors);
    tlm_buf_str(b, "},\"cnt\":{");
    counters(b, s);
    tlm_buf_str(b, "},\"dis\":[");
    tlm_format_disabled(b, s->drive.can_started, false);
    tlm_buf_str(b, "]}\n");
    return !b->overflow;
}
