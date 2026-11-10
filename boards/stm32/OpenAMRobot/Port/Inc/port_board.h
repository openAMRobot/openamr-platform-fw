/* SPDX-License-Identifier: MIT */
/*
 * NUCLEO-H723ZG board port: pins, timing and the hooks called from the
 * CubeMX USER CODE blocks in Core/Src/main.c. Everything board-specific lives
 * under boards/stm32/OpenAMRobot/Port; the portable core is in firmware/.
 *
 * Pin assignments follow the NUCLEO-H723ZG pin map (Evans, 25 Oct) and the
 * PR #22 .ioc. Unassigned pins are named constants with TODO(Evans); while a
 * pin is unassigned the port never reads it as valid and never drives it.
 */
#ifndef PORT_BOARD_H
#define PORT_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Marker for a pin that is not assigned yet. */
#define PORT_PIN_UNASSIGNED 0u

typedef struct {
    GPIO_TypeDef *port;   /* NULL while unassigned */
    uint16_t pin;         /* PORT_PIN_UNASSIGNED while unassigned */
    bool active_high;     /* polarity of the named condition at the pin */
} port_pin_t;

/* Safety and charging I/O (pin map section 3.1, P-03 item 22): all TBD.
 * TODO(Evans): assign from the free pins of the .ioc, with polarity:
 * - SAFETY_OK: PNOZ Y32 via ISO1212, pull-down, high = enabled.
 * - ESTOP_BASE: spare NO, 3.3 V pull-up and series resistor.
 * - RESET: NC contact, 3.3 V pull-up; open (high) = pressed.
 * - WCM_FAULT: open collector, 3.3 V pull-up; polarity of the TZBOT output TBD.
 * - RESET_LAMP, CHARGE_REQUEST: TPL7407L inputs, active high, off from power-up. */
#define PORT_PIN_SAFETY_OK      ((port_pin_t){NULL, PORT_PIN_UNASSIGNED, true})
#define PORT_PIN_ESTOP_BASE     ((port_pin_t){NULL, PORT_PIN_UNASSIGNED, true})
#define PORT_PIN_RESET_PRESSED  ((port_pin_t){NULL, PORT_PIN_UNASSIGNED, true})
#define PORT_PIN_WCM_FAULT      ((port_pin_t){NULL, PORT_PIN_UNASSIGNED, true})
#define PORT_PIN_RESET_LAMP     ((port_pin_t){NULL, PORT_PIN_UNASSIGNED, true})
#define PORT_PIN_CHARGE_REQUEST ((port_pin_t){NULL, PORT_PIN_UNASSIGNED, true})

/* IMU: SPI4 (PE12 SCK, PE13 MISO, PE14 MOSI), chip select PE11 (software). */
#define PORT_IMU_CS_PORT GPIOE
#define PORT_IMU_CS_PIN GPIO_PIN_11
/* TODO(Evans): IMU INT1 data-ready EXTI pin (EV_ICM-42688-P CN1 pin 3).
 * Until assigned, the IMU is polled from the background loop. */
#define PORT_PIN_IMU_INT1 ((port_pin_t){NULL, PORT_PIN_UNASSIGNED, true})

/* Control loop and watchdog. */
#define PORT_CONTROL_PERIOD_US 20000u   /* 50 Hz (Gate A baseline) */
#define PORT_IWDG_TIMEOUT_MS 100u       /* TODO(Evans): watchdog timeout */

/* Hooks for main.c USER CODE blocks. */
void port_capture_reset_cause(void);          /* USER CODE BEGIN 1, before anything clears RCC_RSR */
void port_fdcan_start(FDCAN_HandleTypeDef *h, bool extended_ids); /* FDCANx_Init 2 */
void port_app_init(void);                     /* end of USER CODE BEGIN 2 */
void port_app_background(void);               /* USER CODE BEGIN 3, every pass of the main loop */

/* Host-side entry points for the micro-ROS subscriptions (not wired tonight). */
void port_on_cmd_vel(float linear_m_s, float angular_rad_s);
void port_on_host_heartbeat(void);
void port_on_sw_stop(bool asserted);
void port_on_charge_command(bool requested);

/* OAMR_COMM_* bits still missing from the commissioning record (0 = complete),
 * for /base/status once the micro-ROS client exists. */
uint32_t port_commissioning_missing(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_BOARD_H */
