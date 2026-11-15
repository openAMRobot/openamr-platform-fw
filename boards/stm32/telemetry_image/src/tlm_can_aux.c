/* SPDX-License-Identifier: MIT */
/*
 * CAN2 (FDCAN2, Daly BMS) and CAN3 (FDCAN3, CTL-017 lift) of the telemetry
 * image: RECEIVE ONLY.
 *
 * Both controllers are started with the generated configuration
 * (MX_FDCAN2_Init, MX_FDCAN3_Init: normal mode, generated bit timing), a
 * receive filter and nothing else. No frame is ever queued for transmission
 * on either bus: this file has no transmit call, and the only transmit entry
 * for these buses, tlm_can_aux_tx() (tlm_can_aux_tx.c), refuses every frame.
 * As a CAN node in normal mode each controller still acknowledges correctly
 * received frames and signals errors at bit level (ISO 11898-1); that is the
 * CAN protocol, not a frame of this image. Bus-monitoring mode would avoid
 * it but leaves a two-node bus without any acknowledging receiver, so it is
 * not used (an owner decision, see README "CAN2 and CAN3").
 *
 * Filters:
 *   FDCAN2: the Daly protocol uses 29-bit identifiers and the generated init
 *           has no extended filter element, so extended data frames that match
 *           no filter go to RX FIFO 0 (global filter) and standard frames are
 *           rejected. The decoder counts what is not a BMS response.
 *   FDCAN3: one standard mask filter, low seven bits = TLM_LIFT_NODE_ID: every
 *           COB-ID of the lift node (EMCY, TPDOs, heartbeat, and the RPDO and
 *           SDO identifiers someone else may use). Everything else is rejected.
 *
 * Reporting only: the decoded values, the heartbeat classification and the
 * counters change what the status line says. Nothing here acts on them.
 */
#include "main.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "oamr/co_hb_consumer.h"
#include "oamr/ctl017_rx.h"
#include "oamr/daly_rx.h"
#include "tlm.h"
#include "tlm_can_aux_tx.h"

extern FDCAN_HandleTypeDef hfdcan2; /* generated main.c */
extern FDCAN_HandleTypeDef hfdcan3;

typedef struct
{
    oamr_co_frame_t frame;
    uint64_t rx_us;
    uint8_t bus; /* tlm_can_aux_bus_t */
} aux_rx_item_t;

static StaticQueue_t s_queue_cb;
static uint8_t s_queue_storage[TLM_CAN_AUX_RX_QUEUE_LEN * sizeof(aux_rx_item_t)];
static QueueHandle_t s_queue;
static StaticSemaphore_t s_lock_cb;
static SemaphoreHandle_t s_lock;

static oamr_ctl017_rx_t s_lift;
static oamr_co_hb_consumer_t s_lift_hb;
static oamr_daly_rx_t s_bms;
static bool s_bms_configured;
static volatile bool s_started[TLM_CAN_AUX_N];
static volatile uint32_t s_rx[TLM_CAN_AUX_N];
static volatile uint32_t s_lost[TLM_CAN_AUX_N];

static FDCAN_HandleTypeDef *handle(unsigned bus)
{
    return bus == TLM_CAN_AUX_BMS ? &hfdcan2 : &hfdcan3;
}

/* ---- receive: FDCAN2/FDCAN3 interrupt 0 (priority 5, generated MSP) ---- */
long tlm_can_aux_rx_isr(void *h, uint32_t its)
{
    BaseType_t woken = pdFALSE;
    unsigned bus;
    if (h == &hfdcan2)
    {
        bus = TLM_CAN_AUX_BMS;
    }
    else if (h == &hfdcan3)
    {
        bus = TLM_CAN_AUX_LIFT;
    }
    else
    {
        return pdFALSE;
    }
    FDCAN_HandleTypeDef *hfdcan = (FDCAN_HandleTypeDef *)h;
    if ((its & FDCAN_IT_RX_FIFO0_MESSAGE_LOST) != 0u)
    {
        s_lost[bus]++;
    }
    if ((its & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) == 0u)
    {
        return pdFALSE;
    }
    FDCAN_RxHeaderTypeDef rh;
    aux_rx_item_t item;
    for (unsigned n = 0u; n < TLM_CAN_AUX_RX_MAX_PER_IRQ &&
                          HAL_FDCAN_GetRxFifoFillLevel(hfdcan, FDCAN_RX_FIFO0) > 0u &&
                          HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &rh, item.frame.data) == HAL_OK;
         n++)
    {
        if (rh.RxFrameType != FDCAN_DATA_FRAME || rh.FDFormat != FDCAN_CLASSIC_CAN)
        {
            continue;
        }
        item.frame.id = rh.Identifier;
        item.frame.extended = rh.IdType == FDCAN_EXTENDED_ID;
        item.frame.dlc = (uint8_t)rh.DataLength; /* classic CAN: 0..8 */
        item.rx_us = tlm_time_us();
        item.bus = (uint8_t)bus;
        s_rx[bus]++;
        if (xQueueSendFromISR(s_queue, &item, &woken) != pdTRUE)
        {
            s_lost[bus]++;
        }
    }
    return woken;
}

static bool start_bus(unsigned bus)
{
    FDCAN_HandleTypeDef *h = handle(bus);
    FDCAN_FilterTypeDef filter = {0};
    filter.IdType = FDCAN_STANDARD_ID;
    filter.FilterIndex = 0u; /* the generated init reserves one standard filter */
    if (bus == TLM_CAN_AUX_LIFT)
    {
        filter.FilterType = FDCAN_FILTER_MASK;
        filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
        filter.FilterID1 = TLM_LIFT_NODE_ID;
        filter.FilterID2 = 0x07Fu; /* node-id bits of a CANopen COB-ID */
    }
    else
    {
        filter.FilterType = FDCAN_FILTER_MASK;
        filter.FilterConfig = FDCAN_FILTER_DISABLE; /* no standard frame wanted */
        filter.FilterID1 = 0u;
        filter.FilterID2 = 0u;
    }
    if (HAL_FDCAN_ConfigFilter(h, &filter) != HAL_OK)
    {
        return false;
    }
    const uint32_t nonmatching_ext = bus == TLM_CAN_AUX_BMS ? FDCAN_ACCEPT_IN_RX_FIFO0 : FDCAN_REJECT;
    if (HAL_FDCAN_ConfigGlobalFilter(h, FDCAN_REJECT, nonmatching_ext, FDCAN_REJECT_REMOTE,
                                     FDCAN_REJECT_REMOTE) != HAL_OK)
    {
        return false;
    }
    if (HAL_FDCAN_ActivateNotification(
            h, FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_MESSAGE_LOST, 0u) != HAL_OK)
    {
        return false;
    }
    return HAL_FDCAN_Start(h) == HAL_OK;
}

void tlm_can_aux_init(void)
{
    s_queue = xQueueCreateStatic(TLM_CAN_AUX_RX_QUEUE_LEN, sizeof(aux_rx_item_t), s_queue_storage,
                                 &s_queue_cb);
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_cb);

    const oamr_ctl017_rx_config_t lift_cfg = {.node_id = TLM_LIFT_NODE_ID};
    (void)oamr_ctl017_rx_init(&s_lift, &lift_cfg);
    const oamr_co_hb_config_t hb_cfg = {
        .node_id = TLM_LIFT_NODE_ID,
        .timeout_us = (TLM_LIFT_HB_TIMEOUT_SET != 0) ? TLM_LIFT_HB_TIMEOUT_US : 0u,
    };
    (void)oamr_co_hb_init(&s_lift_hb, &hb_cfg, tlm_time_us());
    const oamr_daly_rx_config_t bms_cfg = {
        .protocol_confirmed = TLM_DALY_PROTOCOL_CONFIRMED != 0,
        .addresses_set = TLM_DALY_ADDR_SET != 0,
        .bms_addr = TLM_DALY_BMS_ADDR,
        .host_addr = TLM_DALY_HOST_ADDR,
        .n_cells = TLM_DALY_N_CELLS,
        .n_temps = TLM_DALY_N_TEMPS,
    };
    s_bms_configured = oamr_daly_rx_init(&s_bms, &bms_cfg);
}

void tlm_can_aux_rx_task(void *arg)
{
    (void)arg;
    for (unsigned bus = 0u; bus < (unsigned)TLM_CAN_AUX_N; bus++)
    {
        s_started[bus] = start_bus(bus);
    }
    for (;;)
    {
        aux_rx_item_t item;
        if (xQueueReceive(s_queue, &item, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }
        (void)xSemaphoreTake(s_lock, portMAX_DELAY);
        if (item.bus == TLM_CAN_AUX_LIFT)
        {
            (void)oamr_ctl017_rx_on_frame(&s_lift, &item.frame, item.rx_us);
            (void)oamr_co_hb_on_frame(&s_lift_hb, &item.frame, item.rx_us);
        }
        else
        {
            oamr_daly_rx_frame_t f;
            f.id = item.frame.id;
            f.extended = item.frame.extended;
            f.dlc = item.frame.dlc;
            for (unsigned i = 0u; i < 8u; i++)
            {
                f.data[i] = item.frame.data[i];
            }
            (void)oamr_daly_rx_on_frame(&s_bms, &f, item.rx_us);
        }
        (void)xSemaphoreGive(s_lock);
    }
}

static const char *nmt_text(const oamr_co_hb_consumer_t *hb)
{
    if (!hb->configured || !hb->seen)
    {
        return "unknown";
    }
    switch (hb->last_state)
    {
    case OAMR_CO_NMT_BOOTUP:
        return "bootup";
    case OAMR_CO_NMT_STOPPED:
        return "stopped";
    case OAMR_CO_NMT_OPERATIONAL:
        return "operational";
    case OAMR_CO_NMT_PRE_OPERATIONAL:
        return "pre_operational";
    default:
        return "unknown";
    }
}

static void age(tlm_opt_u64_t *o, bool known, uint64_t stamp, uint64_t now_us)
{
    o->known = known && now_us >= stamp;
    o->v = o->known ? now_us - stamp : 0u;
}

void tlm_can_aux_snapshot(tlm_aux_status_t *out, uint64_t now_us)
{
    (void)xSemaphoreTake(s_lock, portMAX_DELAY);
    out->can2_started = s_started[TLM_CAN_AUX_BMS];
    out->can3_started = s_started[TLM_CAN_AUX_LIFT];

    switch (oamr_co_hb_classify(&s_lift_hb, now_us))
    {
    case OAMR_CO_HB_WAITING:
        out->lift_hb = "waiting";
        break;
    case OAMR_CO_HB_ALIVE:
        out->lift_hb = "alive";
        break;
    case OAMR_CO_HB_LOST:
        out->lift_hb = "lost";
        break;
    case OAMR_CO_HB_NOT_CONFIGURED:
    default:
        out->lift_hb = "disabled";
        break;
    }
    age(&out->lift_hb_age_us, s_lift_hb.configured && s_lift_hb.seen, s_lift_hb.last_seen_us, now_us);
    out->lift_nmt = nmt_text(&s_lift_hb);
    out->lift_hb_n = s_lift_hb.heartbeats;
    out->lift_hb_lost = s_lift_hb.lost_events;

    const oamr_ctl017_rx_data_t *v = &s_lift.data;
    out->lift_state = oamr_ctl017_state_name(&s_lift);
    out->lift_pos.known = oamr_ctl017_position_known(&s_lift);
    out->lift_pos.v = out->lift_pos.known ? v->position_raw : 0u;
    age(&out->lift_age_us, v->status_seen, v->status_us, now_us);
    out->lift_flags_known = v->status_seen;
    out->lift_flags = v->status_seen ? v->flags : 0u;
    out->lift_s2_known = v->status2_seen;
    out->lift_vel = v->status2_seen ? v->velocity_raw : 0;
    out->lift_ma = v->status2_seen ? v->motor_current_ma : 0u;
    out->lift_faults =
        v->status2_seen ? ((uint32_t)v->faults_f01_f08 | (((uint32_t)v->aux_flags & 1u) << 8)) : 0u;
    out->lift_status = s_lift.counters.status;
    out->lift_gaps = s_lift.counters.counter_gaps;
    out->lift_bad = s_lift.counters.malformed + s_lift_hb.malformed;
    out->lift_emcy = s_lift.counters.emcy;

    /* Battery: values only when the decoder is usable (configured AND the
     * protocol confirmed by the owner). While locked, nothing is reported. */
    const bool usable = oamr_daly_rx_usable(&s_bms);
    out->bms = !s_bms_configured ? "disabled" : (usable ? "usable" : "locked");
    out->bms_values_known = usable && s_bms.data.soc_seen;
    if (out->bms_values_known)
    {
        out->bms_mv = (uint32_t)(s_bms.data.pack_voltage_v * 1000.0f + 0.5f);
        const float da = s_bms.data.current_a * 10.0f;
        out->bms_da = (int32_t)(da >= 0.0f ? da + 0.5f : da - 0.5f);
        out->bms_soc_pm = (uint32_t)(s_bms.data.soc_percent * 10.0f + 0.5f);
    }
    else
    {
        out->bms_mv = 0u;
        out->bms_da = 0;
        out->bms_soc_pm = 0u;
    }
    age(&out->bms_age_us, out->bms_values_known, s_bms.data.soc_stamp_us, now_us);
    out->bms_dec = s_bms.counters.decoded;
    out->bms_foreign = s_bms.counters.foreign;
    out->bms_bad = s_bms.counters.malformed;
    out->bms_unknown = s_bms.counters.unknown_id;
    (void)xSemaphoreGive(s_lock);

    out->c2_rx = s_rx[TLM_CAN_AUX_BMS];
    out->c3_rx = s_rx[TLM_CAN_AUX_LIFT];
    out->c2_lost = s_lost[TLM_CAN_AUX_BMS];
    out->c3_lost = s_lost[TLM_CAN_AUX_LIFT];
    out->c2_tx_ref = tlm_can_aux_tx_refused(TLM_CAN_AUX_BMS);
    out->c3_tx_ref = tlm_can_aux_tx_refused(TLM_CAN_AUX_LIFT);
}
