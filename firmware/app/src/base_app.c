/* SPDX-License-Identifier: MIT */
/*
 * Base controller application. See oamr/base_app.h. The control tick is the
 * same sequence as the NUCLEO-H723ZG port (port_app.c control_tick):
 *   bus-off supervision -> safety inputs -> safety core -> motion core
 *   (motion_allowed passed explicitly) -> ZLAC adapter -> Daly request ->
 *   odometry from the measured wheel speeds.
 */
#include "oamr/base_app.h"

#include <string.h>

#define RPM_PER_RAD_S 9.549296586f
/* Longest gap between two drive feedback samples that is still integrated.
 * The adapter reads 606Ch once per SDO cycle (about 120 ms at 50 Hz). */
#define ODOM_MAX_GAP_US 500000u

static void backend_set(void *ctx, float l, float r)
{
    oamr_app_t *app = (oamr_app_t *)ctx;
    app->zlac_backend.set_wheel_velocity(app->zlac_backend.ctx, l, r);
}

static bool backend_ready(void *ctx)
{
    oamr_app_t *app = (oamr_app_t *)ctx;
    return app->zlac_enabled && oamr_can_health_usable(&app->can1_health) &&
           app->zlac_backend.is_ready(app->zlac_backend.ctx);
}

static oamr_wheel_feedback_t backend_feedback(void *ctx)
{
    oamr_app_t *app = (oamr_app_t *)ctx;
    return app->zlac_backend.feedback(app->zlac_backend.ctx);
}

void oamr_app_init(oamr_app_t *app, const oamr_app_board_t *board, uint64_t now_us)
{
    memset(app, 0, sizeof(*app));
    app->boot_id = board->boot_id;
    app->reset_cause = board->reset_cause;
    app->sw_stop = true; /* asserted at boot (E-01 rev 3) */
    app->now_us = now_us;
    app->odom_max_gap_us = ODOM_MAX_GAP_US;

    oamr_can_health_config_t hc;
    oamr_can_health_default_config(&hc);
    oamr_can_health_init(&app->can1_health, &hc);
    oamr_can_health_init(&app->can2_health, &hc);

    app->commissioning = board->commissioning;
    app->commissioning_missing = oamr_commissioning_missing(&app->commissioning);
    /* Same rule as the STM32 port: the drive is configured once its own
     * fields are ruled; motion needs the complete record. */
    app->zlac_enabled = (app->commissioning_missing &
                         (OAMR_COMM_ZLAC_RAMPS | OAMR_COMM_ZLAC_LINK_LOSS)) == 0u;

    oamr_zlac_config_t zc;
    oamr_zlac_default_config(&zc);
    oamr_zlac_apply_commissioning(&zc, &app->commissioning);
    oamr_zlac_init(&app->zlac, &zc, &board->can1, now_us);
    app->zlac_ctx.zlac = &app->zlac;
    app->zlac_ctx.now_us = board->now_us;
    app->zlac_ctx.clock_ctx = board->clock_ctx;
    app->zlac_backend = oamr_zlac_backend(&app->zlac_ctx);
    app->backend.set_wheel_velocity = backend_set;
    app->backend.is_ready = backend_ready;
    app->backend.feedback = backend_feedback;
    app->backend.ctx = app;

    oamr_daly_config_t dc;
    oamr_daly_default_config(&dc);
    oamr_daly_init(&app->daly, &dc, &board->can2);

    oamr_motion_config_t mc;
    oamr_motion_default_config(&mc);
    oamr_commissioning_apply_motion(&app->commissioning, &mc);
    oamr_motion_init(&app->motion, &mc);

    oamr_sc_config_t scc;
    oamr_sc_default_config(&scc);
    oamr_sc_init(&app->safety, &scc);

    app->imu_result = oamr_icm_init(&app->imu, &board->imu_spi);
    oamr_odom_init(&app->odom);
}

void oamr_app_on_can1_frame(oamr_app_t *app, const oamr_can_frame_t *f, uint64_t now_us)
{
    if (app->zlac_enabled) {
        oamr_zlac_on_frame(&app->zlac, f, now_us);
    }
}

void oamr_app_on_can2_frame(oamr_app_t *app, const oamr_can_frame_t *f, uint64_t now_us)
{
    (void)oamr_daly_on_frame(&app->daly, f, now_us);
}

static void fill_safety_inputs(oamr_app_t *app, const oamr_app_gpio_t *g)
{
    oamr_sc_inputs_t *in = &app->sc_in;
    in->gpio_valid = g->gpio_valid;
    in->safety_ok = g->safety_ok;
    in->estop_base_active = g->estop_base_active;
    in->reset_pressed = g->reset_pressed;
    in->wcm_fault = g->wcm_fault;
    in->estop_column = g->estop_column;
    in->estop_column_seen = g->estop_column_seen;
    in->estop_column_rx_us = g->estop_column_rx_us;
    in->host_seen = app->host_seen;
    in->host_rx_us = app->host_rx_us;
    in->sw_stop = oamr_commissioning_sw_stop(&app->commissioning, app->sw_stop);
    in->charge_cmd = app->charge_cmd;

    const oamr_bms_status_t *bms = oamr_daly_status(&app->daly);
    in->bms_seen = bms->have_data;
    in->bms_fault = bms->fault;
    in->bms_rx_us = bms->stamp_us;

    const oamr_wheel_feedback_t fb = app->zlac.feedback;
    in->encoder_seen = fb.valid;
    in->encoder_rx_us = fb.stamp_us;
    in->encoder_left_rpm = fb.left_rad_s * RPM_PER_RAD_S;
    in->encoder_right_rpm = fb.right_rad_s * RPM_PER_RAD_S;
}

oamr_app_actions_t oamr_app_control_tick(oamr_app_t *app, uint64_t now_us,
                                         const oamr_app_gpio_t *gpio,
                                         bool can1_bus_off, bool can2_bus_off)
{
    oamr_app_actions_t act;
    memset(&act, 0, sizeof(act));
    app->now_us = now_us;

    act.restart_can1 = oamr_can_health_update(&app->can1_health, now_us, can1_bus_off) ==
                       OAMR_CAN_ACTION_RESTART;
    act.restart_can2 = oamr_can_health_update(&app->can2_health, now_us, can2_bus_off) ==
                       OAMR_CAN_ACTION_RESTART;

    fill_safety_inputs(app, gpio);
    const oamr_sc_outputs_t *sc = oamr_sc_step(&app->safety, &app->sc_in, now_us);
    act.reset_lamp = sc->reset_lamp;
    act.charge_request = sc->charge_request;
    act.controller_reset = sc->controller_reset;

    /* The single motion gate, checked at the single wheel write. */
    const bool motion_allowed = oamr_sc_motion_allowed(&app->safety);
    /* TODO(Evans): hold_and_move from the aux status (arms holding a pose). */
    (void)oamr_motion_tick(&app->motion, now_us, motion_allowed, false, &app->backend);

    if (app->zlac_enabled && oamr_can_health_usable(&app->can1_health)) {
        oamr_zlac_tick(&app->zlac, now_us);
    }
    if (app->zlac.state == OAMR_ZLAC_OPERATIONAL) {
        app->zlac_seen_operational = true;
    }
    if (oamr_can_health_usable(&app->can2_health)) {
        oamr_daly_poll(&app->daly, now_us);
    }

    const oamr_wheel_feedback_t fb = app->zlac.feedback;
    if (fb.valid && fb.stamp_us != app->odom_last_fb_us) {
        app->odom_last_fb_us = fb.stamp_us;
        (void)oamr_odom_update(&app->odom, fb.left_rad_s, fb.right_rad_s, fb.stamp_us,
                               app->motion.cfg.wheel_radius_m, app->motion.cfg.track_width_m,
                               app->odom_max_gap_us);
    }
    return act;
}

void oamr_app_background(oamr_app_t *app, uint64_t now_us)
{
    if (app->imu_result == OAMR_ICM_OK) {
        (void)oamr_icm_poll(&app->imu, now_us);
    }
}

void oamr_app_cmd_vel(oamr_app_t *app, float linear_m_s, float angular_rad_s, uint64_t now_us)
{
    oamr_motion_cmd_vel(&app->motion, linear_m_s, angular_rad_s, now_us);
}

void oamr_app_host_heartbeat(oamr_app_t *app, uint64_t now_us)
{
    app->host_seen = true;
    app->host_rx_us = now_us;
    oamr_motion_host_heartbeat(&app->motion, now_us);
}

void oamr_app_sw_stop(oamr_app_t *app, bool asserted)
{
    app->sw_stop = asserted;
}

void oamr_app_charge_cmd(oamr_app_t *app, bool requested)
{
    app->charge_cmd = requested;
}

bool oamr_app_take_imu_sample(oamr_app_t *app, oamr_imu_sample_t *out)
{
    return app->imu_result == OAMR_ICM_OK && oamr_icm_take_sample(&app->imu, out);
}
