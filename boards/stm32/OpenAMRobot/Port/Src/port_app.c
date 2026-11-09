/* SPDX-License-Identifier: MIT */
/*
 * NUCLEO-H723ZG application glue: owns the core objects, runs the control
 * tick from TIM6 at the highest application priority, and services the slow
 * work from the background loop. Task model (I8 baseline item 6): the control
 * tick never blocks; transport and IMU polling run in the background loop.
 *
 * Control tick, every 20 ms:
 *   drain CAN1 and CAN2 RX -> bus-off supervision -> safety inputs ->
 *   safety_core -> motion core (motion_allowed passed explicitly) -> ZLAC
 *   adapter -> Daly request -> feed the IWDG (the only place it is fed).
 *
 * What cannot work yet, by design:
 * - The six safety and charging pins are unassigned (TODO(Evans)), so the
 *   safety core sees INPUT_INVALID and stays LOCKED: motion_allowed is false.
 * - The second E-stop status from CTL-017 (CAN3) is not relayed yet, so it is
 *   UNKNOWN, which the safety core treats as an E-stop diagnostic.
 * - No micro-ROS client: the port_on_* entry points exist for its callbacks.
 * So this build cannot move the wheels. That is intended until the owner has
 * assigned the pins and ruled on D-24.
 */
#include "port_board.h"
#include "port_internal.h"

#include "oamr/can_health.h"
#include "oamr/daly_bms.h"
#include "oamr/icm42688.h"
#include "oamr/motion.h"
#include "oamr/safety_core.h"
#include "oamr/zlac8015d.h"

extern FDCAN_HandleTypeDef hfdcan1;
extern FDCAN_HandleTypeDef hfdcan2;

#define RPM_PER_RAD_S 9.549296586f

static oamr_motion_t s_motion;
static oamr_zlac_t s_zlac;
static oamr_zlac_backend_ctx_t s_zlac_ctx;
static oamr_motor_backend_t s_zlac_backend;
static oamr_motor_backend_t s_backend;
static oamr_daly_t s_daly;
static oamr_sc_t s_safety;
static oamr_sc_inputs_t s_sc_in;
static oamr_can_health_t s_can1_health;
static oamr_can_health_t s_can2_health;
static oamr_icm_t s_imu;
static oamr_icm_result_t s_imu_result = OAMR_ICM_BUS_ERROR;
static oamr_transport_t s_transport;
static bool s_transport_open;
static volatile bool s_app_ready;

/* Host inputs, written from the background loop with interrupts masked. */
static bool s_host_seen;
static uint64_t s_host_rx_us;
static bool s_sw_stop = true;  /* asserted at boot (E-01 rev 3) */
static bool s_charge_cmd;

static bool pin_assigned(port_pin_t p)
{
    return p.port != NULL && p.pin != PORT_PIN_UNASSIGNED;
}

static bool read_pin(port_pin_t p, bool *value)
{
    if (!pin_assigned(p)) {
        return false;
    }
    const bool high = HAL_GPIO_ReadPin(p.port, p.pin) == GPIO_PIN_SET;
    *value = (high == p.active_high);
    return true;
}

static void write_pin(port_pin_t p, bool on)
{
    if (pin_assigned(p)) {
        HAL_GPIO_WritePin(p.port, p.pin, (on == p.active_high) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}

/* Motor backend seen by the motion core: the ZLAC adapter, and ready only
 * while CAN1 is usable. */
static void backend_set(void *ctx, float l, float r)
{
    (void)ctx;
    s_zlac_backend.set_wheel_velocity(s_zlac_backend.ctx, l, r);
}

static bool backend_ready(void *ctx)
{
    (void)ctx;
    return oamr_can_health_usable(&s_can1_health) && s_zlac_backend.is_ready(s_zlac_backend.ctx);
}

static oamr_wheel_feedback_t backend_feedback(void *ctx)
{
    (void)ctx;
    return s_zlac_backend.feedback(s_zlac_backend.ctx);
}

static void read_safety_inputs(void)
{
    bool ok = true;
    ok = read_pin(PORT_PIN_SAFETY_OK, &s_sc_in.safety_ok) && ok;
    ok = read_pin(PORT_PIN_ESTOP_BASE, &s_sc_in.estop_base_active) && ok;
    ok = read_pin(PORT_PIN_RESET_PRESSED, &s_sc_in.reset_pressed) && ok;
    ok = read_pin(PORT_PIN_WCM_FAULT, &s_sc_in.wcm_fault) && ok;
    s_sc_in.gpio_valid = ok;

    /* TODO(Evans): second E-stop observation from CTL-017 over CAN3. */
    s_sc_in.estop_column = OAMR_SC_COLUMN_UNKNOWN;
    s_sc_in.estop_column_seen = false;

    s_sc_in.host_seen = s_host_seen;
    s_sc_in.host_rx_us = s_host_rx_us;
    s_sc_in.sw_stop = s_sw_stop;
    s_sc_in.charge_cmd = s_charge_cmd;

    const oamr_bms_status_t *bms = oamr_daly_status(&s_daly);
    s_sc_in.bms_seen = bms->have_data;
    s_sc_in.bms_fault = bms->fault;
    s_sc_in.bms_rx_us = bms->stamp_us;

    const oamr_wheel_feedback_t fb = s_zlac.feedback;
    s_sc_in.encoder_seen = fb.valid;
    s_sc_in.encoder_rx_us = fb.stamp_us;
    s_sc_in.encoder_left_rpm = fb.left_rad_s * RPM_PER_RAD_S;
    s_sc_in.encoder_right_rpm = fb.right_rad_s * RPM_PER_RAD_S;
}

static void supervise_can(FDCAN_HandleTypeDef *h, oamr_can_health_t *health, uint64_t now)
{
    if (oamr_can_health_update(health, now, port_can_bus_off(h)) == OAMR_CAN_ACTION_RESTART) {
        port_can_restart(h);
    }
}

static void control_tick(void)
{
    const uint64_t now = port_clock_now_us(NULL);
    oamr_can_frame_t f;
    while (port_can_receive(&hfdcan1, &f)) {
        oamr_zlac_on_frame(&s_zlac, &f, now);
    }
    while (port_can_receive(&hfdcan2, &f)) {
        (void)oamr_daly_on_frame(&s_daly, &f, now);
    }
    supervise_can(&hfdcan1, &s_can1_health, now);
    supervise_can(&hfdcan2, &s_can2_health, now);

    read_safety_inputs();
    const oamr_sc_outputs_t *sc = oamr_sc_step(&s_safety, &s_sc_in, now);
    write_pin(PORT_PIN_RESET_LAMP, sc->reset_lamp);
    write_pin(PORT_PIN_CHARGE_REQUEST, sc->charge_request);
    if (sc->controller_reset) {
        NVIC_SystemReset();
    }

    /* The single motion gate, checked at the single wheel write. */
    const bool motion_allowed = oamr_sc_motion_allowed(&s_safety);
    /* TODO(Evans): hold_and_move from the aux status (arms holding a pose). */
    (void)oamr_motion_tick(&s_motion, now, motion_allowed, false, &s_backend);

    if (oamr_can_health_usable(&s_can1_health)) {
        oamr_zlac_tick(&s_zlac, now);
    }
    if (oamr_can_health_usable(&s_can2_health)) {
        oamr_daly_poll(&s_daly, now);
    }
    port_iwdg_feed();
}

void TIM6_DAC_IRQHandler(void)
{
    if (TIM6->SR & TIM_SR_UIF) {
        TIM6->SR = ~TIM_SR_UIF;
        if (s_app_ready) {
            control_tick();
        }
    }
}

void port_app_init(void)
{
    /* Both TPL7407L outputs stay off until the safety core drives them. */
    write_pin(PORT_PIN_RESET_LAMP, false);
    write_pin(PORT_PIN_CHARGE_REQUEST, false);

    port_clock_init();
    const uint64_t now = port_clock_now_us(NULL);

    oamr_can_health_config_t hc;
    oamr_can_health_default_config(&hc);
    oamr_can_health_init(&s_can1_health, &hc);
    oamr_can_health_init(&s_can2_health, &hc);

    oamr_zlac_config_t zc;
    oamr_zlac_default_config(&zc);
    const oamr_can_tx_t can1 = port_can_tx(&hfdcan1);
    oamr_zlac_init(&s_zlac, &zc, &can1, now);
    s_zlac_ctx.zlac = &s_zlac;
    s_zlac_ctx.now_us = port_clock_now_us;
    s_zlac_ctx.clock_ctx = NULL;
    s_zlac_backend = oamr_zlac_backend(&s_zlac_ctx);
    s_backend.set_wheel_velocity = backend_set;
    s_backend.is_ready = backend_ready;
    s_backend.feedback = backend_feedback;
    s_backend.ctx = NULL;

    oamr_daly_config_t dc;
    oamr_daly_default_config(&dc);
    const oamr_can_tx_t can2 = port_can_tx(&hfdcan2);
    oamr_daly_init(&s_daly, &dc, &can2);

    oamr_motion_config_t mc;
    oamr_motion_default_config(&mc);
    oamr_motion_init(&s_motion, &mc);

    oamr_sc_config_t scc;
    oamr_sc_default_config(&scc);
    oamr_sc_init(&s_safety, &scc);

    const oamr_spi_t spi = port_imu_spi();
    s_imu_result = oamr_icm_init(&s_imu, &spi);

    s_transport = port_serial_transport();
    s_transport_open = s_transport.open(s_transport.ctx);

    port_iwdg_start(PORT_IWDG_TIMEOUT_MS);
    s_app_ready = true;
    port_tick_timer_start(PORT_CONTROL_PERIOD_US);
}

void port_app_background(void)
{
    if (!s_app_ready) {
        return;
    }
    if (s_imu_result == OAMR_ICM_OK) {
        /* INT1 is unassigned: poll the data-ready flag. Samples are taken by
         * the micro-ROS publisher once it exists (oamr_icm_take_sample). */
        (void)oamr_icm_poll(&s_imu, port_clock_now_us(NULL));
    }
    /* TODO(Evans): micro-ROS executor spin over s_transport (serial first). */
    (void)s_transport_open;
}

void port_on_cmd_vel(float linear_m_s, float angular_rad_s)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    oamr_motion_cmd_vel(&s_motion, linear_m_s, angular_rad_s, port_clock_now_us(NULL));
    __set_PRIMASK(primask);
}

void port_on_host_heartbeat(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const uint64_t now = port_clock_now_us(NULL);
    s_host_seen = true;
    s_host_rx_us = now;
    oamr_motion_host_heartbeat(&s_motion, now);
    __set_PRIMASK(primask);
}

void port_on_sw_stop(bool asserted)
{
    s_sw_stop = asserted;
}

void port_on_charge_command(bool requested)
{
    s_charge_cmd = requested;
}
