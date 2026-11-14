/* SPDX-License-Identifier: MIT */
/*
 * CAN1 of the telemetry image: read-only client of the drive (node
 * TLM_DRIVE_NODE_ID).
 *
 * Receives: SDO responses (580h + node) and heartbeats (700h + node); every
 * other identifier is rejected by the FDCAN1 filter.
 * Sends: SDO initiate-upload requests (byte 0 = 40h) issued by
 * oamr_co_upl_tick(), and nothing else. can1_send() checks every frame and
 * refuses anything that is not such a request, so no other frame can reach
 * the bus through this file even if a caller were added later.
 *
 * Reporting only: the heartbeat consumer and the timeouts change what the
 * status line says. Nothing here acts on a lost drive.
 */
#include "main.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "oamr/co_cobid.h"
#include "oamr/co_hb_consumer.h"
#include "oamr/co_upload_sched.h"
#include "oamr/zlac_telemetry.h"
#include "tlm.h"

extern FDCAN_HandleTypeDef hfdcan1; /* generated main.c */

/* Objects read in a cycle; the order is the order of the task statement. */
enum { OBJ_VEL_CH1 = 0, OBJ_VEL_CH2, OBJ_STATUSWORD, OBJ_FAULT, OBJ_RESOLUTION, OBJ_COUNT };
static const oamr_co_object_t s_objects[OBJ_COUNT] = {
    [OBJ_VEL_CH1] = {OAMR_ZLAC_OD_ACTUAL_VELOCITY, 0x01u},
    [OBJ_VEL_CH2] = {OAMR_ZLAC_OD_ACTUAL_VELOCITY, 0x02u},
    [OBJ_STATUSWORD] = {OAMR_ZLAC_OD_STATUSWORD, 0x00u},
    [OBJ_FAULT] = {OAMR_ZLAC_OD_FAULT_CODE, 0x00u},
    [OBJ_RESOLUTION] = {OAMR_ZLAC_OD_SPEED_RESOLUTION, OAMR_ZLAC_OD_SPEED_RESOLUTION_SUB},
};

typedef struct {
    oamr_co_frame_t frame;
    uint64_t rx_us;
} can1_rx_item_t;

static StaticQueue_t s_queue_cb;
static uint8_t s_queue_storage[TLM_CAN1_RX_QUEUE_LEN * sizeof(can1_rx_item_t)];
static QueueHandle_t s_queue;
static StaticSemaphore_t s_lock_cb;
static SemaphoreHandle_t s_lock;

static oamr_co_upl_t s_upl;
static oamr_co_hb_consumer_t s_hb;
static bool s_started;
static uint32_t s_last_seq[OBJ_COUNT];

static volatile uint32_t s_rx, s_rx_lost;
static uint32_t s_tx, s_tx_failed, s_tx_refused;

/* ---- transmit: upload requests only ---- */
static bool can1_send(void *ctx, const oamr_co_frame_t *f)
{
    (void)ctx;
    const bool is_upload_request =
        !f->extended && f->id == OAMR_CO_COB_SDO_RX_BASE + TLM_DRIVE_NODE_ID && f->dlc == 8u &&
        f->data[0] == OAMR_CO_SDO_CCS_UPLOAD_INIT && f->data[4] == 0u && f->data[5] == 0u &&
        f->data[6] == 0u && f->data[7] == 0u;
    if (!is_upload_request) {
        s_tx_refused++;
        return false;
    }
    if (!s_started) {
        s_tx_failed++;
        return false;
    }
    FDCAN_TxHeaderTypeDef h = {0};
    h.Identifier = f->id;
    h.IdType = FDCAN_STANDARD_ID;
    h.TxFrameType = FDCAN_DATA_FRAME;
    h.DataLength = FDCAN_DLC_BYTES_8;
    h.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    h.BitRateSwitch = FDCAN_BRS_OFF;
    h.FDFormat = FDCAN_CLASSIC_CAN;
    h.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &h, f->data) != HAL_OK) {
        s_tx_failed++;
        return false;
    }
    s_tx++;
    return true;
}

/* ---- receive: FDCAN1 interrupt (priority 5, generated MSP) ---- */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    BaseType_t woken = pdFALSE;
    if (hfdcan != &hfdcan1) {
        return;
    }
    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_MESSAGE_LOST) != 0u) {
        s_rx_lost++;
    }
    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != 0u) {
        FDCAN_RxHeaderTypeDef h;
        can1_rx_item_t item;
        while (HAL_FDCAN_GetRxFifoFillLevel(hfdcan, FDCAN_RX_FIFO0) > 0u &&
               HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &h, item.frame.data) == HAL_OK) {
            if (h.RxFrameType != FDCAN_DATA_FRAME || h.FDFormat != FDCAN_CLASSIC_CAN) {
                continue;
            }
            item.frame.id = h.Identifier;
            item.frame.extended = h.IdType == FDCAN_EXTENDED_ID;
            item.frame.dlc = (uint8_t)h.DataLength; /* classic CAN: FDCAN_DLC_BYTES_0..8 are 0..8 */
            item.rx_us = tlm_time_us();
            s_rx++;
            if (xQueueSendFromISR(s_queue, &item, &woken) != pdTRUE) {
                s_rx_lost++;
            }
        }
    }
    portYIELD_FROM_ISR(woken);
}

static bool can1_start(void)
{
    FDCAN_FilterTypeDef filter = {0};
    filter.IdType = FDCAN_STANDARD_ID;
    filter.FilterIndex = 0u; /* the generated init reserves one standard filter */
    filter.FilterType = FDCAN_FILTER_DUAL;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1 = OAMR_CO_COB_SDO_TX_BASE + TLM_DRIVE_NODE_ID;
    filter.FilterID2 = OAMR_CO_COB_HEARTBEAT_BASE + TLM_DRIVE_NODE_ID;
    if (HAL_FDCAN_ConfigFilter(&hfdcan1, &filter) != HAL_OK) {
        return false;
    }
    if (HAL_FDCAN_ConfigGlobalFilter(&hfdcan1, FDCAN_REJECT, FDCAN_REJECT, FDCAN_REJECT_REMOTE,
                                     FDCAN_REJECT_REMOTE) != HAL_OK) {
        return false;
    }
    if (HAL_FDCAN_ActivateNotification(&hfdcan1,
                                       FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_MESSAGE_LOST,
                                       0u) != HAL_OK) {
        return false;
    }
    return HAL_FDCAN_Start(&hfdcan1) == HAL_OK; /* normal mode, generated bit timing */
}

void tlm_can1_init(void)
{
    s_queue = xQueueCreateStatic(TLM_CAN1_RX_QUEUE_LEN, sizeof(can1_rx_item_t), s_queue_storage,
                                 &s_queue_cb);
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_cb);

    /* An unset parameter leaves its module NOT_CONFIGURED: it then sends and
     * classifies nothing, and the status line reports the feature disabled. */
    const oamr_co_upl_config_t upl_cfg = {
        .node_id = TLM_DRIVE_NODE_ID,
        .objects = s_objects,
        .n_objects = OBJ_COUNT,
        .timeout_us = (TLM_SDO_TIMEOUT_SET != 0) ? TLM_SDO_TIMEOUT_US : 0u,
        .max_retries = TLM_SDO_RETRIES,
        .max_retries_set = TLM_SDO_RETRIES_SET != 0,
        .send = can1_send,
        .send_ctx = 0,
    };
    (void)oamr_co_upl_init(&s_upl, &upl_cfg);

    const oamr_co_hb_config_t hb_cfg = {
        .node_id = TLM_DRIVE_NODE_ID,
        .timeout_us = (TLM_HB_TIMEOUT_SET != 0) ? TLM_HB_TIMEOUT_US : 0u,
    };
    (void)oamr_co_hb_init(&s_hb, &hb_cfg, tlm_time_us());
}

void tlm_can1_task(void *arg)
{
    (void)arg;
    s_started = can1_start();

    TickType_t deadline = xTaskGetTickCount();
    for (;;) {
        deadline += pdMS_TO_TICKS(TLM_CAN1_TICK_MS);
        /* Handle frames as they arrive until the next tick is due. */
        for (unsigned handled = 0u; handled < 2u * TLM_CAN1_RX_QUEUE_LEN; handled++) {
            const TickType_t now = xTaskGetTickCount();
            const TickType_t left = deadline - now;
            const TickType_t wait = (left <= pdMS_TO_TICKS(TLM_CAN1_TICK_MS)) ? left : 0u;
            can1_rx_item_t item;
            if (xQueueReceive(s_queue, &item, wait) != pdTRUE) {
                break;
            }
            (void)xSemaphoreTake(s_lock, portMAX_DELAY);
            (void)oamr_co_upl_on_frame(&s_upl, &item.frame, item.rx_us);
            (void)oamr_co_hb_on_frame(&s_hb, &item.frame, item.rx_us);
            (void)xSemaphoreGive(s_lock);
        }
        (void)xSemaphoreTake(s_lock, portMAX_DELAY);
        const uint64_t now_us = tlm_time_us();
        oamr_co_upl_tick(&s_upl, now_us); /* at most one upload request */
        (void)oamr_co_hb_classify(&s_hb, now_us);
        (void)xSemaphoreGive(s_lock);
    }
}

/* ---- snapshot for the reporter ---- */
static const char *validity_text(oamr_tlm_validity_t v, const oamr_co_sample_t *smp)
{
    switch (v) {
    case OAMR_TLM_VALID: return "valid";
    case OAMR_TLM_NOT_READ: return "not_read";
    case OAMR_TLM_BAD_SIZE: return "bad_size";
    case OAMR_TLM_STALE: return "stale";
    case OAMR_TLM_UNCONFIRMED: return "unconfirmed";
    case OAMR_TLM_READ_FAILED:
        switch (smp != 0 ? smp->status : OAMR_CO_SAMPLE_NONE) {
        case OAMR_CO_SAMPLE_TIMEOUT: return "timeout";
        case OAMR_CO_SAMPLE_ABORTED: return "aborted";
        case OAMR_CO_SAMPLE_PROTOCOL: return "protocol";
        case OAMR_CO_SAMPLE_TX_FAILED: return "tx_failed";
        default: return "read_failed";
        }
    case OAMR_TLM_NOT_CONFIGURED:
    default: return "unset";
    }
}

static void fill_obj(tlm_obj_t *o, unsigned idx, const oamr_tlm_meta_t *meta, bool raw_known,
                     int64_t raw, unsigned hex_digits, uint64_t now_us)
{
    const oamr_co_sample_t *smp = oamr_co_upl_sample(&s_upl, (uint8_t)idx);
    o->validity = validity_text(meta->validity, smp);
    o->raw_known = raw_known;
    o->raw = raw_known ? raw : 0;
    o->hex_digits = hex_digits;
    o->age_us.known = false;
    o->age_us.v = 0u;
    o->reads = 0u;
    o->abort_known = false;
    o->abort_code = 0u;
    o->state = 0;
    if (smp != 0) {
        o->reads = smp->seq;
        if (smp->status == OAMR_CO_SAMPLE_VALID && now_us >= smp->sample_us) {
            o->age_us.known = true;
            o->age_us.v = now_us - smp->sample_us;
        }
        if (smp->status == OAMR_CO_SAMPLE_ABORTED) {
            o->abort_known = true;
            o->abort_code = smp->abort_code;
        }
        s_last_seq[idx] = smp->seq;
    }
}

static const char *nmt_text(const oamr_co_hb_consumer_t *hb)
{
    if (!hb->configured || !hb->seen) {
        return "unknown";
    }
    switch (hb->last_state) {
    case OAMR_CO_NMT_BOOTUP: return "bootup";
    case OAMR_CO_NMT_STOPPED: return "stopped";
    case OAMR_CO_NMT_OPERATIONAL: return "operational";
    case OAMR_CO_NMT_PRE_OPERATIONAL: return "pre_operational";
    default: return "unknown";
    }
}

void tlm_can1_snapshot(tlm_drive_status_t *out, uint64_t now_us)
{
    const oamr_tlm_config_t cfg = {
        .max_age_us = (TLM_DRIVE_MAX_AGE_SET != 0) ? TLM_DRIVE_MAX_AGE_US : 0u,
    };
    oamr_tlm_resolution_t res;
    oamr_tlm_velocity_t v1, v2;
    oamr_tlm_statusword_t sw;
    oamr_tlm_fault_t fault;

    (void)xSemaphoreTake(s_lock, portMAX_DELAY);

    out->can_started = s_started;
    out->sdo_enabled = s_upl.state != OAMR_CO_UPL_NOT_CONFIGURED;

    /* heartbeat */
    out->hb_age_us.known = false;
    out->hb_age_us.v = 0u;
    switch (oamr_co_hb_classify(&s_hb, now_us)) {
    case OAMR_CO_HB_WAITING: out->hb_state = "waiting"; break;
    case OAMR_CO_HB_ALIVE: out->hb_state = "alive"; break;
    case OAMR_CO_HB_LOST: out->hb_state = "lost"; break;
    case OAMR_CO_HB_NOT_CONFIGURED:
    default: out->hb_state = "disabled"; break;
    }
    if (s_hb.configured && s_hb.seen && now_us >= s_hb.last_seen_us) {
        out->hb_age_us.known = true;
        out->hb_age_us.v = now_us - s_hb.last_seen_us;
    }
    out->nmt = nmt_text(&s_hb);
    out->hb_count = s_hb.heartbeats;
    out->hb_bootups = s_hb.bootups;
    out->hb_lost_events = s_hb.lost_events;
    out->hb_malformed = s_hb.malformed;

    /* objects, each with its own validity */
    oamr_tlm_decode_resolution(&cfg, oamr_co_upl_sample(&s_upl, OBJ_RESOLUTION),
                               TLM_SPEED_RESOLUTION_REQUIRED, TLM_SPEED_RESOLUTION_SET != 0,
                               s_last_seq[OBJ_RESOLUTION], now_us, &res);
    oamr_tlm_decode_velocity(&cfg, oamr_co_upl_sample(&s_upl, OBJ_VEL_CH1), &res,
                             s_last_seq[OBJ_VEL_CH1], now_us, &v1);
    oamr_tlm_decode_velocity(&cfg, oamr_co_upl_sample(&s_upl, OBJ_VEL_CH2), &res,
                             s_last_seq[OBJ_VEL_CH2], now_us, &v2);
    oamr_tlm_decode_statusword(&cfg, oamr_co_upl_sample(&s_upl, OBJ_STATUSWORD),
                               s_last_seq[OBJ_STATUSWORD], now_us, &sw);
    oamr_tlm_decode_fault(&cfg, oamr_co_upl_sample(&s_upl, OBJ_FAULT), s_last_seq[OBJ_FAULT],
                          now_us, &fault);

    fill_obj(&out->wheel_ch1, OBJ_VEL_CH1, &v1.meta,
             v1.meta.validity == OAMR_TLM_VALID || v1.meta.validity == OAMR_TLM_UNCONFIRMED, v1.raw,
             0u, now_us);
    fill_obj(&out->wheel_ch2, OBJ_VEL_CH2, &v2.meta,
             v2.meta.validity == OAMR_TLM_VALID || v2.meta.validity == OAMR_TLM_UNCONFIRMED, v2.raw,
             0u, now_us);
    fill_obj(&out->statusword, OBJ_STATUSWORD, &sw.meta, sw.meta.validity == OAMR_TLM_VALID, sw.raw,
             4u, now_us);
    if (sw.meta.validity == OAMR_TLM_VALID) {
        out->statusword.state = oamr_402_state_name(sw.state);
    }
    fill_obj(&out->fault, OBJ_FAULT, &fault.meta, fault.meta.validity == OAMR_TLM_VALID, fault.raw,
             8u, now_us);
    fill_obj(&out->resolution, OBJ_RESOLUTION, &res.meta, res.meta.validity == OAMR_TLM_VALID,
             res.raw, 0u, now_us);

    /* counters */
    out->sdo_ok = out->sdo_timeouts = out->sdo_aborts = out->sdo_protocol = 0u;
    for (uint8_t i = 0u; i < OBJ_COUNT; i++) {
        const oamr_co_upl_counters_t *c = oamr_co_upl_counters(&s_upl, i);
        if (c != 0) {
            out->sdo_ok += c->ok;
            out->sdo_timeouts += c->timeouts;
            out->sdo_aborts += c->aborts;
            out->sdo_protocol += c->protocol_errors;
        }
    }
    out->sdo_unsolicited = s_upl.unsolicited;
    out->sdo_wrong_object = s_upl.wrong_object;
    out->can_tx = s_tx;
    out->can_tx_failed = s_tx_failed;
    out->can_tx_refused = s_tx_refused;

    (void)xSemaphoreGive(s_lock);

    out->can_rx = s_rx;
    out->can_rx_lost = s_rx_lost;

    /* Bus state as the controller reports it. No recovery is attempted here. */
    FDCAN_ProtocolStatusTypeDef ps;
    out->bus_state_known = s_started && HAL_FDCAN_GetProtocolStatus(&hfdcan1, &ps) == HAL_OK;
    out->bus_off = out->bus_state_known && ps.BusOff != 0u;
}
