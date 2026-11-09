/* SPDX-License-Identifier: MIT */
/*
 * Base controller application: the board-independent composition of the core
 * (motion, CAN health, commissioning, odometry), the device adapters
 * (ZLAC8015D on CAN1, Daly BMS on CAN2, ICM-42688-P on SPI) and the E-01 rev 3
 * safety core. It is the same sequence as the NUCLEO-H723ZG control tick in
 * boards/stm32/OpenAMRobot/Port/Src/port_app.c; the host board port
 * (boards/host) runs it as a Linux process for the software-in-the-loop chain.
 * TODO(Evans): switch port_app.c to this module so both boards run one copy.
 *
 * The board port owns the peripherals and calls in:
 *   - oamr_app_on_can1_frame / oamr_app_on_can2_frame for every received frame,
 *   - oamr_app_control_tick once per control period with the sampled safety
 *     inputs and the bus-off state of both controllers,
 *   - oamr_app_background from the background loop (IMU poll),
 *   - oamr_app_cmd_vel / host_heartbeat / sw_stop / charge_cmd from the host
 *     transport (micro-ROS callbacks).
 * The board must serialise these calls (interrupt masking on the MCU, a mutex
 * on the host). Nothing here blocks.
 *
 * Status (oamr_app_snapshot) follows the semantic groups of the I8 Base
 * Controller Interface Proposal rev 3. Field-level names are a local proposal
 * until openamr_base_msgs exists (TODO(Evans)).
 */
#ifndef OAMR_BASE_APP_H
#define OAMR_BASE_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "oamr/can.h"
#include "oamr/can_health.h"
#include "oamr/commissioning.h"
#include "oamr/daly_bms.h"
#include "oamr/icm42688.h"
#include "oamr/motion.h"
#include "oamr/odometry.h"
#include "oamr/reset_cause.h"
#include "oamr/safety_core.h"
#include "oamr/zlac8015d.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Contract version of the stand-in status (I8: uint16 starting at 1). */
#define OAMR_APP_CONTRACT_VERSION 1u

/* Fault codes, group 18001 (I8 rev 3 section 4.1, interfaces #31 Q2).
 * PROPOSAL: the registry format and the numbering inside the group are open
 * (I8 decision 5). TODO(Evans): register these in openamrobot-interfaces. */
typedef enum {
    OAMR_FAULT_CMD_TIMEOUT = 18001,          /* non-latching, /cmd_vel older than 200 ms */
    OAMR_FAULT_HOST_LINK_LOST = 18002,       /* host heartbeat older than 500 ms */
    OAMR_FAULT_DRIVE_COMM_LOST = 18003,      /* ZLAC heartbeat lost or repeated SDO failures */
    OAMR_FAULT_DRIVE_FAULT = 18004,          /* ZLAC 603Fh non-zero, latched */
    OAMR_FAULT_DRIVE_CONFIG_FAILED = 18005,  /* ZLAC boot read-back mismatch, latched */
    OAMR_FAULT_CAN1_BUS_OFF = 18006,         /* CAN1 bus-off, restarting */
    OAMR_FAULT_CAN1_FAILED = 18007,          /* CAN1 bus-off too often, latched */
    OAMR_FAULT_CAN2_BUS_OFF = 18008,
    OAMR_FAULT_CAN2_FAILED = 18009,
    OAMR_FAULT_BMS_STALE = 18010,            /* Daly data missing or older than the stale time */
    OAMR_FAULT_BMS_FAULT = 18011,            /* Daly level-2 alarm or hardware failure bit */
    OAMR_FAULT_IMU_INVALID = 18012,          /* last IMU read failed or returned the invalid code */
    OAMR_FAULT_IMU_NOT_PRESENT = 18013,      /* WHO_AM_I or configuration check failed at boot */
    OAMR_FAULT_SAFETY_INHIBIT = 18014,       /* safety core not in RUN: motion inhibited */
    OAMR_FAULT_NOT_COMMISSIONED = 18015      /* commissioning record incomplete */
} oamr_fault_code_t;

#define OAMR_APP_MAX_FAULTS 16u

/* IMU health for the status: sample age and consecutive bad reads before
 * IMU_INVALID. TODO(Evans): rule both with the INT1 data-ready design
 * (200 Hz ODR). */
#define OAMR_APP_IMU_MAX_AGE_US 100000u
#define OAMR_APP_IMU_MAX_CONSECUTIVE_BAD 1u

typedef enum {
    OAMR_CTRL_UNKNOWN = 0,
    OAMR_CTRL_INITIALIZING,
    OAMR_CTRL_READY,
    OAMR_CTRL_DEGRADED,
    OAMR_CTRL_FAULTED
} oamr_controller_state_t;

typedef struct {
    uint16_t code;            /* oamr_fault_code_t */
    bool latched;             /* needs an operator action to clear */
    bool inhibits_motion;
} oamr_app_fault_t;

/* One complete status snapshot, taken under the board's lock. */
typedef struct {
    uint16_t contract_version;
    uint32_t boot_id;
    uint64_t now_us;
    oamr_controller_state_t controller_state;
    bool recovery_required;          /* LOCKED: a RESET press is needed before RUN */
    bool command_accepted;           /* the applied command is OK */
    /* safety core (E-01 rev 3) */
    oamr_sc_state_t safety_state;
    uint32_t safety_reasons;
    uint32_t safety_first_reason;
    bool motion_allowed;
    bool reset_lamp;
    bool charge_request;
    /* command and host link */
    bool host_link_ok;
    oamr_motion_applied_t applied;
    /* drive */
    oamr_zlac_state_t drive_state;
    bool drive_enabled;              /* adapter started (commissioning ZLAC fields present) */
    bool drive_ready;
    uint32_t drive_fault_raw;        /* 603Fh */
    oamr_wheel_feedback_t wheels;
    /* buses */
    oamr_can_health_state_t can1;
    oamr_can_health_state_t can2;
    uint32_t can1_bus_off_total;
    uint32_t can2_bus_off_total;
    /* battery */
    bool bms_fresh;
    oamr_bms_status_t bms;
    /* IMU */
    bool imu_present;
    bool imu_healthy;
    oamr_imu_counters_t imu_counters;
    /* odometry */
    oamr_odom_t odom;
    /* configuration */
    uint32_t commissioning_missing;  /* OAMR_COMM_* bits */
    oamr_reset_cause_t reset_cause;
    bool powered_debug;              /* always false: no powered-debug path exists */
    /* faults */
    uint8_t n_faults;
    bool faults_overflow;
    oamr_app_fault_t faults[OAMR_APP_MAX_FAULTS];
} oamr_app_status_t;

/* Safety and charging inputs as sampled by the board port (polarity applied).
 * gpio_valid false while any pin is unassigned or unread. */
typedef struct {
    bool gpio_valid;
    bool safety_ok;
    bool estop_base_active;
    bool reset_pressed;
    bool wcm_fault;
    oamr_sc_column_estop_t estop_column;
    bool estop_column_seen;
    uint64_t estop_column_rx_us;
} oamr_app_gpio_t;

/* What the board must do after a control tick. */
typedef struct {
    bool reset_lamp;
    bool charge_request;
    bool controller_reset;           /* reset the MCU (or exit the host process) now */
    bool restart_can1;
    bool restart_can2;
} oamr_app_actions_t;

typedef struct {
    oamr_motion_t motion;
    oamr_zlac_t zlac;
    oamr_zlac_backend_ctx_t zlac_ctx;
    oamr_motor_backend_t zlac_backend;
    oamr_motor_backend_t backend;
    oamr_daly_t daly;
    oamr_sc_t safety;
    oamr_sc_inputs_t sc_in;
    oamr_can_health_t can1_health;
    oamr_can_health_t can2_health;
    oamr_icm_t imu;
    oamr_icm_result_t imu_result;
    oamr_odom_t odom;
    uint64_t odom_last_fb_us;
    oamr_commissioning_record_t commissioning;
    uint32_t commissioning_missing;
    bool zlac_enabled;
    bool zlac_seen_operational;
    uint64_t now_us;                 /* time of the last control tick */
    uint64_t odom_max_gap_us;
    uint32_t boot_id;
    oamr_reset_cause_t reset_cause;
    /* host inputs */
    bool host_seen;
    uint64_t host_rx_us;
    bool sw_stop;
    bool charge_cmd;
} oamr_app_t;

typedef struct {
    oamr_can_tx_t can1;
    oamr_can_tx_t can2;
    oamr_spi_t imu_spi;
    oamr_commissioning_record_t commissioning;
    uint32_t boot_id;
    oamr_reset_cause_t reset_cause;
    /* clock used by the ZLAC ready check (same clock as every now_us below) */
    uint64_t (*now_us)(void *ctx);
    void *clock_ctx;
} oamr_app_board_t;

void oamr_app_init(oamr_app_t *app, const oamr_app_board_t *board, uint64_t now_us);

void oamr_app_on_can1_frame(oamr_app_t *app, const oamr_can_frame_t *f, uint64_t now_us);
void oamr_app_on_can2_frame(oamr_app_t *app, const oamr_can_frame_t *f, uint64_t now_us);

oamr_app_actions_t oamr_app_control_tick(oamr_app_t *app, uint64_t now_us,
                                         const oamr_app_gpio_t *gpio,
                                         bool can1_bus_off, bool can2_bus_off);

void oamr_app_background(oamr_app_t *app, uint64_t now_us);

void oamr_app_cmd_vel(oamr_app_t *app, float linear_m_s, float angular_rad_s, uint64_t now_us);
void oamr_app_host_heartbeat(oamr_app_t *app, uint64_t now_us);
void oamr_app_sw_stop(oamr_app_t *app, bool asserted);
void oamr_app_charge_cmd(oamr_app_t *app, bool requested);

/* Take the next valid IMU sample, if any (never an invalid one). */
bool oamr_app_take_imu_sample(oamr_app_t *app, oamr_imu_sample_t *out);

void oamr_app_snapshot(const oamr_app_t *app, uint64_t now_us, oamr_app_status_t *out);

const char *oamr_app_fault_name(uint16_t code);
const char *oamr_app_controller_state_name(oamr_controller_state_t s);
const char *oamr_app_safety_state_name(oamr_sc_state_t s);
const char *oamr_app_applied_state_name(oamr_applied_state_t s);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_BASE_APP_H */
