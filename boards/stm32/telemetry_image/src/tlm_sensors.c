/* SPDX-License-Identifier: MIT */
/*
 * Sensor UARTs of the telemetry image. Receive only.
 *
 *   UART4, UART5, UART7, UART8  -> MB7060 parser (9600 8N1, RX inverted;
 *                                   generated configuration)
 *   USART2, USART6              -> TF-Luna parser (115200 8N1)
 *
 * The UART interrupt (generated handler, HAL_UART_IRQHandler) stores each
 * byte in a ring; the sensors task drains the rings and feeds the parsers
 * (src/tlm_uart_rx.h for the ring and the restart policy).
 *
 * Every HAL_UART_Receive_IT call is checked. A call that does not return
 * HAL_OK is counted and stops the port; the sensors task restarts it with at
 * most TLM_UART_RX_RESTART_MAX failed attempts in a row and then reports the
 * port failed. A full ring discards that port's buffered bytes, resets its
 * parser and counts the loss; the parser resynchronises on the next header.
 *
 * This image sends no MB7060 trigger pulse. The parser accepts a frame as a
 * sample only while armed by a trigger, so it is never armed here: every
 * well-formed frame is counted (as unsolicited) and the range is reported
 * unknown. Nothing is ever written to a sensor.
 */
#include "main.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "oamr/freshness.h"
#include "oamr/mb7060.h"
#include "oamr/tfluna.h"
#include "tlm.h"
#include "tlm_sched.h"
#include "tlm_uart_rx.h"

extern UART_HandleTypeDef huart4, huart5, huart7, huart8, huart2, huart6; /* generated main.c */

#define N_PORTS (TLM_N_MB7060 + TLM_N_TFLUNA)

typedef struct
{
    UART_HandleTypeDef *huart;
    const char *name;
    uint8_t rx_byte;
    tlm_ring_t ring;
    tlm_uart_rx_t rx;
    volatile uint32_t errors;
    volatile bool flush; /* a UART error happened: drop the partial frame */
} port_t;

/* Ports 0..3 are MB7060, ports 4..5 are TF-Luna. */
static port_t s_port[N_PORTS] = {
    {.huart = &huart4, .name = "uart4"},  {.huart = &huart5, .name = "uart5"},
    {.huart = &huart7, .name = "uart7"},  {.huart = &huart8, .name = "uart8"},
    {.huart = &huart2, .name = "usart2"}, {.huart = &huart6, .name = "usart6"},
};

static oamr_mb_t s_mb[TLM_N_MB7060];
static bool s_mb_seen[TLM_N_MB7060];
static uint64_t s_mb_stamp_us[TLM_N_MB7060];

static oamr_tfl_t s_tfl[TLM_N_TFLUNA];
static bool s_tfl_configured[TLM_N_TFLUNA];
static oamr_fresh_t s_tfl_fresh[TLM_N_TFLUNA];
static oamr_tfl_sample_t s_tfl_last[TLM_N_TFLUNA];
static bool s_tfl_seen[TLM_N_TFLUNA];
static uint64_t s_tfl_stamp_us[TLM_N_TFLUNA];

static StaticSemaphore_t s_lock_cb;
static SemaphoreHandle_t s_lock;

/* ---- interrupt side ---- */
static port_t *port_of(const UART_HandleTypeDef *huart)
{
    for (unsigned i = 0u; i < N_PORTS; i++)
    {
        if (s_port[i].huart == huart)
        {
            return &s_port[i];
        }
    }
    return 0;
}

/* Re-arm reception for the next byte; a failure stops the port. */
static void rearm(port_t *p)
{
    if (HAL_UART_Receive_IT(p->huart, &p->rx_byte, 1u) != HAL_OK)
    {
        tlm_uart_rx_lost(&p->rx);
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    port_t *p = port_of(huart);
    if (p == 0)
    {
        return;
    }
    tlm_ring_put(&p->ring, p->rx_byte);
    rearm(p);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    port_t *p = port_of(huart);
    if (p == 0)
    {
        return;
    }
    p->errors++;
    p->flush = true;
    /* After a blocking error (overrun, framing, noise) the HAL has ended the
     * reception and the port must be re-armed; after a non-blocking one the
     * reception goes on and must not be started twice. */
    if (huart->RxState == HAL_UART_STATE_READY)
    {
        rearm(p);
    }
}

/* ---- task side ---- */
void tlm_sensors_init(void)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_cb);
    for (unsigned i = 0u; i < N_PORTS; i++)
    {
        tlm_ring_init(&s_port[i].ring);
        tlm_uart_rx_init(&s_port[i].rx, (uint8_t)TLM_UART_RX_RESTART_MAX);
    }
    for (unsigned i = 0u; i < TLM_N_MB7060; i++)
    {
        oamr_mb_init(&s_mb[i]); /* not armed, and never armed in this image */
    }
    for (unsigned i = 0u; i < TLM_N_TFLUNA; i++)
    {
        const oamr_tfl_config_t cfg = {
            .unit =
                (TLM_TFLUNA_UNIT_SET != 0) ? (oamr_tfl_unit_t)TLM_TFLUNA_UNIT : OAMR_TFL_UNIT_UNSET,
        };
        const oamr_fresh_config_t fresh = {
            .stale_after_us = (TLM_TFLUNA_STALE_SET != 0) ? TLM_TFLUNA_STALE_US : 0u,
        };
        s_tfl_configured[i] = oamr_tfl_init(&s_tfl[i], &cfg);
        (void)oamr_fresh_init(&s_tfl_fresh[i], &fresh);
    }
}

static void feed_mb(unsigned i, uint8_t byte, uint64_t now_us)
{
    oamr_mb_sample_t unused;
    const oamr_mb_push_t r = oamr_mb_push(&s_mb[i], byte, &unused);
    if (r == OAMR_MB_PUSH_UNSOLICITED)
    {
        /* A well-formed frame arrived. Without a trigger it answers no
         * reading, so only its arrival time is kept, not its range. */
        s_mb_seen[i] = true;
        s_mb_stamp_us[i] = now_us;
    }
}

static void feed_tfl(unsigned i, uint8_t byte, uint64_t now_us)
{
    oamr_tfl_sample_t smp;
    if (oamr_tfl_push(&s_tfl[i], byte, &smp) == OAMR_TFL_PUSH_SAMPLE)
    {
        s_tfl_last[i] = smp;
        s_tfl_seen[i] = true;
        s_tfl_stamp_us[i] = now_us;
        oamr_fresh_mark(&s_tfl_fresh[i], now_us);
    }
}

static void reset_parser(unsigned i)
{
    if (i < TLM_N_MB7060)
    {
        oamr_mb_flush(&s_mb[i]);
    }
    else
    {
        oamr_tfl_flush(&s_tfl[i - TLM_N_MB7060]);
    }
}

/* (Re)start reception of every stopped port; bounded by the policy. */
static void restart_stopped_ports(void)
{
    for (unsigned i = 0u; i < N_PORTS; i++)
    {
        port_t *p = &s_port[i];
        if (!tlm_uart_rx_needs_start(&p->rx))
        {
            continue;
        }
        /* The UART interrupt (priority 5) is masked here, so it cannot change
         * the port state between the call and the bookkeeping. */
        taskENTER_CRITICAL();
        const bool ok = HAL_UART_Receive_IT(p->huart, &p->rx_byte, 1u) == HAL_OK;
        tlm_uart_rx_retry_result(&p->rx, ok);
        taskEXIT_CRITICAL();
    }
}

/* Deadline accounting of the poll (tlm_sched.h), written by the sensors task
 * under s_lock: a missed poll period is counted and reported, never made up. */
static tlm_sched_stats_t s_poll;

void tlm_sensors_task(void *arg)
{
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(TLM_SENSORS_POLL_MS);
    TickType_t wake = xTaskGetTickCount();
    for (;;)
    {
        restart_stopped_ports();
        const uint64_t now_us = tlm_time_us();
        (void)xSemaphoreTake(s_lock, portMAX_DELAY);
        for (unsigned i = 0u; i < N_PORTS; i++)
        {
            port_t *p = &s_port[i];
            /* Overflow first: what is buffered is discarded, not parsed. */
            const bool overflowed = tlm_ring_take_overflow(&p->ring);
            if (overflowed || p->flush)
            {
                p->flush = false;
                reset_parser(i);
            }
            uint8_t byte;
            while (tlm_ring_get(&p->ring, &byte))
            {
                if (i < TLM_N_MB7060)
                {
                    feed_mb(i, byte, now_us);
                }
                else
                {
                    feed_tfl(i - TLM_N_MB7060, byte, now_us);
                }
            }
        }
        s_poll.runs++;
        (void)xSemaphoreGive(s_lock);
        if (xTaskDelayUntil(&wake, period) == pdFALSE)
        {
            (void)xSemaphoreTake(s_lock, portMAX_DELAY);
            wake = (TickType_t)tlm_sched_late(&s_poll, (uint32_t)wake, (uint32_t)xTaskGetTickCount(),
                                              (uint32_t)period);
            (void)xSemaphoreGive(s_lock);
        }
    }
}

static const char *tfl_status_text(oamr_tfl_status_t s)
{
    switch (s) {
    case OAMR_TFL_OK: return "ok";
    case OAMR_TFL_NO_RETURN: return "no_return";
    case OAMR_TFL_WEAK: return "weak";
    case OAMR_TFL_OVEREXPOSED: return "overexposed";
    case OAMR_TFL_BELOW_MIN: return "below_min";
    case OAMR_TFL_ABOVE_MAX: return "above_max";
    case OAMR_TFL_UNKNOWN:
    default: return "unknown";
    }
}

void tlm_sensors_snapshot(tlm_sensors_status_t *out, uint64_t now_us)
{
    (void)xSemaphoreTake(s_lock, portMAX_DELAY);
    out->uart_errors = 0u;
    out->ring_overflows = 0u;
    out->ring_lost_bytes = 0u;
    out->rx_start_failures = 0u;
    out->rx_restarts = 0u;
    out->poll_runs = s_poll.runs;
    out->poll_missed = s_poll.missed;
    for (unsigned i = 0u; i < N_PORTS; i++) {
        const port_t *p = &s_port[i];
        out->uart_errors += p->errors;
        out->ring_overflows += p->ring.ovf_events;
        out->ring_lost_bytes += tlm_ring_lost(&p->ring);
        out->rx_start_failures += p->rx.start_failures;
        out->rx_restarts += p->rx.starts > 0u ? p->rx.starts - 1u : 0u;
        out->port_name[i] = p->name;
        out->rx_state[i] = tlm_uart_rx_state_name(p->rx.state);
        out->rx_failed[i] = p->rx.state == TLM_UART_RX_FAILED;
    }
    for (unsigned i = 0u; i < TLM_N_MB7060; i++) {
        tlm_mb_status_t *m = &out->mb[i];
        m->name = s_port[i].name;
        m->frames = s_mb[i].counters.unsolicited + s_mb[i].counters.frames_ok;
        m->malformed = s_mb[i].counters.malformed;
        m->age_us.known = s_mb_seen[i] && now_us >= s_mb_stamp_us[i];
        m->age_us.v = m->age_us.known ? now_us - s_mb_stamp_us[i] : 0u;
    }
    for (unsigned i = 0u; i < TLM_N_TFLUNA; i++) {
        tlm_tfl_status_t *t = &out->tfl[i];
        t->name = s_port[TLM_N_MB7060 + i].name;
        t->frames = s_tfl[i].counters.frames_ok;
        t->checksum_errors = s_tfl[i].counters.checksum_errors;
        t->valid_known = false;
        t->valid = false;
        t->distance_known = false;
        t->distance_mm = 0u;
        t->age_us.known = false;
        t->age_us.v = 0u;
        if (!s_tfl_configured[i]) {
            t->status = "disabled";
            continue;
        }
        if (!s_tfl_seen[i]) {
            t->status = "never";
            continue;
        }
        t->status = tfl_status_text(s_tfl_last[i].status);
        if (now_us >= s_tfl_stamp_us[i]) {
            t->age_us.known = true;
            t->age_us.v = now_us - s_tfl_stamp_us[i];
        }
        /* Valid needs a usable frame AND a freshness verdict. Without a
         * stale limit the verdict is unknown, and so is valid. */
        switch (oamr_fresh_classify(&s_tfl_fresh[i], now_us)) {
        case OAMR_FRESH_FRESH:
            t->valid_known = true;
            t->valid = s_tfl_last[i].valid;
            break;
        case OAMR_FRESH_STALE:
            t->status = "stale";
            t->valid_known = true;
            t->valid = false;
            break;
        case OAMR_FRESH_NEVER:
        case OAMR_FRESH_UNKNOWN:
        default:
            break;
        }
        if (t->valid_known && t->valid) {
            t->distance_known = true;
            t->distance_mm = s_tfl_last[i].distance_mm;
        }
    }
    (void)xSemaphoreGive(s_lock);
}
