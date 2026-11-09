/* SPDX-License-Identifier: MIT */
/*
 * Status snapshot for /base/status (I8 rev 3 semantic groups). Faults are
 * listed in a fixed order (by code) so the bounded list is deterministic;
 * entries past OAMR_APP_MAX_FAULTS set faults_overflow.
 */
#include "oamr/base_app.h"

#include <string.h>


static void add(oamr_app_status_t *s, uint16_t code, bool latched, bool inhibits)
{
    if (s->n_faults >= OAMR_APP_MAX_FAULTS) {
        s->faults_overflow = true;
        return;
    }
    oamr_app_fault_t *f = &s->faults[s->n_faults++];
    f->code = code;
    f->latched = latched;
    f->inhibits_motion = inhibits;
}

void oamr_app_snapshot(const oamr_app_t *app, uint64_t now_us, oamr_app_status_t *s)
{
    memset(s, 0, sizeof(*s));
    s->contract_version = OAMR_APP_CONTRACT_VERSION;
    s->boot_id = app->boot_id;
    s->now_us = now_us;

    const oamr_sc_outputs_t *sc = &app->safety.out;
    s->safety_state = sc->state;
    s->safety_reasons = sc->reasons;
    s->safety_first_reason = sc->first_reason;
    s->motion_allowed = sc->motion_allowed;
    s->reset_lamp = sc->reset_lamp;
    s->charge_request = sc->charge_request;

    s->host_link_ok = app->host_seen && now_us >= app->host_rx_us &&
                      (now_us - app->host_rx_us) < app->motion.cfg.host_timeout_us;
    s->applied = app->motion.applied;
    s->command_accepted = s->applied.state == OAMR_APPLIED_OK;

    s->drive_state = app->zlac.state;
    s->drive_enabled = app->zlac_enabled;
    s->drive_ready = app->zlac_enabled && oamr_can_health_usable(&app->can1_health) &&
                     oamr_zlac_ready(&app->zlac, now_us);
    s->drive_fault_raw = app->zlac.fault.raw;
    s->wheels = app->zlac.feedback;

    s->can1 = app->can1_health.state;
    s->can2 = app->can2_health.state;
    s->can1_bus_off_total = app->can1_health.bus_off_total;
    s->can2_bus_off_total = app->can2_health.bus_off_total;

    s->bms_fresh = oamr_daly_fresh(&app->daly, now_us);
    s->bms = *oamr_daly_status(&app->daly);

    s->imu_present = app->imu_result == OAMR_ICM_OK;
    s->imu_healthy = s->imu_present &&
                     oamr_icm_healthy(&app->imu, now_us, OAMR_APP_IMU_MAX_AGE_US, OAMR_APP_IMU_MAX_CONSECUTIVE_BAD);
    s->imu_counters = app->imu.counters;

    s->odom = app->odom;
    s->commissioning_missing = app->commissioning_missing;
    s->reset_cause = app->reset_cause;
    s->powered_debug = false;

    /* Fault list, ascending code. */
    const oamr_applied_state_t a = s->applied.state;
    if (a == OAMR_APPLIED_TIMEOUT) {
        add(s, OAMR_FAULT_CMD_TIMEOUT, false, true);
    }
    if (!s->host_link_ok) {
        add(s, OAMR_FAULT_HOST_LINK_LOST, false, true);
    }
    if (app->zlac_enabled && app->zlac.state == OAMR_ZLAC_COMM_LOST) {
        add(s, OAMR_FAULT_DRIVE_COMM_LOST, false, true);
    }
    if (app->zlac_enabled && app->zlac.state == OAMR_ZLAC_FAULT) {
        add(s, OAMR_FAULT_DRIVE_FAULT, true, true);
    }
    if (app->zlac_enabled && app->zlac.state == OAMR_ZLAC_CONFIG_FAILED) {
        add(s, OAMR_FAULT_DRIVE_CONFIG_FAILED, true, true);
    }
    if (s->can1 == OAMR_CAN_BUS_OFF_WAIT || s->can1 == OAMR_CAN_RESTARTING) {
        add(s, OAMR_FAULT_CAN1_BUS_OFF, false, true);
    }
    if (s->can1 == OAMR_CAN_FAILED) {
        add(s, OAMR_FAULT_CAN1_FAILED, true, true);
    }
    if (s->can2 == OAMR_CAN_BUS_OFF_WAIT || s->can2 == OAMR_CAN_RESTARTING) {
        add(s, OAMR_FAULT_CAN2_BUS_OFF, false, true);
    }
    if (s->can2 == OAMR_CAN_FAILED) {
        add(s, OAMR_FAULT_CAN2_FAILED, true, true);
    }
    if (!s->bms_fresh) {
        add(s, OAMR_FAULT_BMS_STALE, false, true);
    }
    if (s->bms.have_data && s->bms.fault) {
        add(s, OAMR_FAULT_BMS_FAULT, false, true);
    }
    if (s->imu_present && !s->imu_healthy) {
        /* E-01 rev 3 does not make the IMU a motion inhibit: reported only. */
        add(s, OAMR_FAULT_IMU_INVALID, false, false);
    }
    if (!s->imu_present) {
        add(s, OAMR_FAULT_IMU_NOT_PRESENT, false, false);
    }
    if (!s->motion_allowed) {
        add(s, OAMR_FAULT_SAFETY_INHIBIT, false, true);
    }
    if (s->commissioning_missing != 0u) {
        add(s, OAMR_FAULT_NOT_COMMISSIONED, false, true);
    }

    /* Controller state (I8 rev 3 section 3.1). PROPOSAL: decision 2 is open. */
    bool any_latched = false;
    for (uint8_t i = 0; i < s->n_faults; i++) {
        any_latched = any_latched || s->faults[i].latched;
    }
    if (any_latched) {
        s->controller_state = OAMR_CTRL_FAULTED;
    } else if (!app->safety.first_tick_done || (app->zlac_enabled && !app->zlac_seen_operational)) {
        s->controller_state = OAMR_CTRL_INITIALIZING;
    } else if (s->n_faults == 0u) {
        s->controller_state = OAMR_CTRL_READY;
    } else {
        s->controller_state = OAMR_CTRL_DEGRADED;
    }
    s->recovery_required = s->safety_state == OAMR_SC_LOCKED;
}

const char *oamr_app_fault_name(uint16_t code)
{
    switch (code) {
    case OAMR_FAULT_CMD_TIMEOUT: return "CMD_TIMEOUT";
    case OAMR_FAULT_HOST_LINK_LOST: return "HOST_LINK_LOST";
    case OAMR_FAULT_DRIVE_COMM_LOST: return "DRIVE_COMM_LOST";
    case OAMR_FAULT_DRIVE_FAULT: return "DRIVE_FAULT";
    case OAMR_FAULT_DRIVE_CONFIG_FAILED: return "DRIVE_CONFIG_FAILED";
    case OAMR_FAULT_CAN1_BUS_OFF: return "CAN1_BUS_OFF";
    case OAMR_FAULT_CAN1_FAILED: return "CAN1_FAILED";
    case OAMR_FAULT_CAN2_BUS_OFF: return "CAN2_BUS_OFF";
    case OAMR_FAULT_CAN2_FAILED: return "CAN2_FAILED";
    case OAMR_FAULT_BMS_STALE: return "BMS_STALE";
    case OAMR_FAULT_BMS_FAULT: return "BMS_FAULT";
    case OAMR_FAULT_IMU_INVALID: return "IMU_INVALID";
    case OAMR_FAULT_IMU_NOT_PRESENT: return "IMU_NOT_PRESENT";
    case OAMR_FAULT_SAFETY_INHIBIT: return "SAFETY_INHIBIT";
    case OAMR_FAULT_NOT_COMMISSIONED: return "NOT_COMMISSIONED";
    default: return "UNKNOWN";
    }
}

const char *oamr_app_controller_state_name(oamr_controller_state_t st)
{
    switch (st) {
    case OAMR_CTRL_INITIALIZING: return "INITIALIZING";
    case OAMR_CTRL_READY: return "READY";
    case OAMR_CTRL_DEGRADED: return "DEGRADED";
    case OAMR_CTRL_FAULTED: return "FAULTED";
    default: return "UNKNOWN";
    }
}

const char *oamr_app_safety_state_name(oamr_sc_state_t st)
{
    switch (st) {
    case OAMR_SC_LOCKED: return "LOCKED";
    case OAMR_SC_RUN: return "RUN";
    case OAMR_SC_CHARGING: return "CHARGING";
    case OAMR_SC_CHARGER_FAULT: return "CHARGER_FAULT";
    default: return "UNKNOWN";
    }
}

const char *oamr_app_applied_state_name(oamr_applied_state_t st)
{
    switch (st) {
    case OAMR_APPLIED_OK: return "OK";
    case OAMR_APPLIED_NO_COMMAND: return "NO_COMMAND";
    case OAMR_APPLIED_TIMEOUT: return "TIMEOUT";
    case OAMR_APPLIED_HOST_LOST: return "HOST_LOST";
    case OAMR_APPLIED_DRIVER_NOT_READY: return "DRIVER_NOT_READY";
    case OAMR_APPLIED_INHIBITED: return "INHIBITED";
    case OAMR_APPLIED_NOT_COMMISSIONED: return "NOT_COMMISSIONED";
    default: return "UNKNOWN";
    }
}
