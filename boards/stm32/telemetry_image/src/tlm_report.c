/* SPDX-License-Identifier: MIT */
/* Reporter of the telemetry image: boot banner, one JSON status line ("st")
 * per TLM_REPORT_PERIOD_MS and one slow line ("sl") every TLM_SLOW_EVERY
 * periods on the console. Caps (tlm_params.h): "st" at most 60 % and "sl" at
 * most 35 % of the characters 115200 baud carries in one period. */
#include "FreeRTOS.h"
#include "task.h"

#include "oamr/reset_cause.h"
#include "tlm.h"
#include "tlm_build_info.h"

#define TLM_PROJECT_NAME "openamr-platform-fw telemetry_image"

/* Banner buffer. A periodic line longer than its cap is replaced by a short
 * error record (and counted) instead of being sent late. */
static char s_line[1664]; /* banner with every disabled feature and its reason: 1.3 kB */
static tlm_status_t s_status;
static tlm_boot_id_t s_boot;

static const char *reset_cause_text(oamr_reset_cause_t c)
{
    switch (c) {
    case OAMR_RESET_CAUSE_POWER_ON: return "power_on";
    case OAMR_RESET_CAUSE_BROWNOUT: return "brownout";
    case OAMR_RESET_CAUSE_WATCHDOG: return "watchdog";
    case OAMR_RESET_CAUSE_SOFTWARE: return "software";
    case OAMR_RESET_CAUSE_PIN: return "pin";
    case OAMR_RESET_CAUSE_UNKNOWN:
    default: return "unknown";
    }
}

void tlm_report_banner(uint32_t rcc_rsr, const tlm_boot_id_t *boot)
{
    const oamr_reset_decoded_t rc = oamr_reset_cause_decode(rcc_rsr);
    tlm_buf_t b;

    s_boot = *boot;
    tlm_buf_init(&b, s_line, sizeof(s_line));
    tlm_buf_str(&b, "\n{\"t\":\"boot\",");
    tlm_buf_kv_str(&b, "project", TLM_PROJECT_NAME);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_str(&b, "commit", TLM_GIT_COMMIT);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_str(&b, "build", TLM_BUILD_TYPE);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_str(&b, "params", TLM_PARAMS_NAME);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_str(&b, "reset_cause", reset_cause_text(rc.cause));
    tlm_buf_str(&b, ",\"rcc_rsr\":\"");
    tlm_buf_hex(&b, rc.flags_raw, 8u);
    tlm_buf_str(&b, "\",");
    tlm_buf_kv_bool(&b, "reset_multiple", rc.multiple_sources);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_bool(&b, "reset_unknown_bits", rc.unknown_bits);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_u64(&b, "boot", boot->id);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_str(&b, "boot_record", boot->retained ? "retained" : "cold");
    tlm_buf_str(&b, ",");
    tlm_buf_kv_u64(&b, "can1_bitrate", TLM_CAN1_BITRATE);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_u64(&b, "drive_node", TLM_DRIVE_NODE_ID);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_u64(&b, "can2_bitrate", TLM_CAN2_BITRATE);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_u64(&b, "can3_bitrate", TLM_CAN3_BITRATE);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_u64(&b, "lift_node", TLM_LIFT_NODE_ID);
    tlm_buf_str(&b, ",");
    tlm_buf_kv_u64(&b, "report_period_ms", TLM_REPORT_PERIOD_MS);
    tlm_buf_str(&b, ",\"disabled\":[");
    tlm_format_disabled(&b, true, true);
    tlm_buf_str(&b, "]}\n");
    if (!b.overflow) {
        (void)tlm_console_write(b.buf, b.len);
    } else {
        tlm_console_write_raw("{\"t\":\"err\",\"e\":\"banner_overflow\"}\n");
    }
}

/* Format one periodic line into s_line within cap; on overflow, an error
 * record of its own. Returns the buffer to send. */
typedef enum
{
    LINE_STATUS,
    LINE_SLOW,
    LINE_AUX
} line_t;

static tlm_buf_t format_line(line_t kind, size_t cap, const char *what, uint32_t *too_long)
{
    tlm_buf_t b;
    tlm_buf_init(&b, s_line, cap);
    const bool ok = kind == LINE_SLOW  ? tlm_format_slow(&s_status, tlm_slow_part(s_status.seq), &b)
                    : kind == LINE_AUX ? tlm_format_aux(&s_status, &b)
                                       : tlm_format_status(&s_status, &b);
    if (!ok)
    {
        /* Never send a cut line: say so in a line of its own. */
        (*too_long)++;
        tlm_buf_init(&b, s_line, sizeof(s_line));
        tlm_buf_str(&b, "{\"t\":\"err\",");
        tlm_buf_kv_u64(&b, "boot", s_boot.id);
        tlm_buf_str(&b, ",");
        tlm_buf_kv_u64(&b, "seq", s_status.seq);
        tlm_buf_str(&b, ",");
        tlm_buf_kv_str(&b, "e", what);
        /* "of":"sl" or "of":"ax" marks the record that replaces a slow or an
         * ax line, so a host keeps the status line sequence apart from it. */
        tlm_buf_str(&b, kind == LINE_SLOW  ? ",\"of\":\"sl\"}\n"
                        : kind == LINE_AUX ? ",\"of\":\"ax\"}\n"
                                           : "}\n");
    }
    return b;
}

void tlm_report_task(void *arg)
{
    (void)arg;
    uint32_t seq = 0u;
    uint32_t overruns = 0u;
    uint32_t dropped = 0u;
    uint32_t too_long = 0u;
    TickType_t wake = xTaskGetTickCount();

    for (;;)
    {
        if (xTaskDelayUntil(&wake, pdMS_TO_TICKS(TLM_REPORT_PERIOD_MS)) == pdFALSE)
        {
            overruns++; /* the previous lines took longer than one period */
        }
        const uint64_t now_us = tlm_time_us();
        s_status.boot = s_boot;
        s_status.seq = seq;
        s_status.uptime_us = now_us;
        s_status.params = TLM_PARAMS_NAME;
        s_status.report_overruns = overruns;
        s_status.lines_dropped = dropped;
        s_status.lines_too_long = too_long;
        tlm_can1_snapshot(&s_status.drive, now_us);
        tlm_sensors_snapshot(&s_status.sensors, now_us);
        tlm_can_aux_snapshot(&s_status.aux, now_us);

        tlm_buf_t b = format_line(LINE_STATUS, TLM_STATUS_LINE_MAX, "line_overflow", &too_long);
        if (!tlm_console_write(b.buf, b.len))
        {
            dropped++;
        }
        if (seq % TLM_SLOW_EVERY == 0u)
        {
            tlm_stack_marks(&s_status.stacks);
            b = format_line(LINE_SLOW, TLM_SLOW_LINE_MAX, "slow_overflow", &too_long);
            if (!tlm_console_write(b.buf, b.len))
            {
                dropped++;
            }
        }
        else if (tlm_aux_due(seq))
        {
            b = format_line(LINE_AUX, TLM_SLOW_LINE_MAX, "ax_overflow", &too_long);
            if (!tlm_console_write(b.buf, b.len))
            {
                dropped++;
            }
        }
        seq++;
    }
}
