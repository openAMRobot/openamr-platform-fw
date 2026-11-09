/* SPDX-License-Identifier: MIT */
/*
 * micro-ROS application layer of the base controller.
 *
 * Portable C on top of rclc and the micro-ROS custom transport. It knows the
 * board only through oamr_transport_t (byte stream to the agent), oamr_clock_t
 * and the hooks below, so the same file builds for the host SIL port
 * (boards/host, pty to the agent) and, later, for the NUCLEO-H723ZG
 * (serial or USB CDC first, issue #21).
 * TODO(Evans): STM32 build. Route: micro_ros_stm32cubemx_utils static library
 * (libmicroros.a built with the same colcon.meta: custom transport, 1 session,
 * history 32/32) linked into the CubeMX project; run oamr_uros_spin_once() from
 * the background loop or a low-priority task, never from the control timer.
 *
 * Topics (I8 rev 3, section 6):
 *   sub  /cmd_vel           geometry_msgs/Twist
 *   pub  /base/status       10 Hz heartbeat + on change, reliable, transient-local depth 1
 *   pub  /base/aux_status   5 Hz heartbeat, reliable, transient-local depth 1
 *   pub  /odom/unfiltered   nav_msgs/Odometry, odom -> base_footprint
 *   pub  /imu/data_raw      sensor_msgs/Imu, frame imu_link, every valid sample
 * Message types: openamr_base_msgs does not exist yet (interfaces #30/#31), so
 * both status topics carry a LOCAL STAND-IN, diagnostic_msgs/DiagnosticStatus
 * with one key per I8 field. TODO(Evans): switch to openamr_base_msgs/BaseStatus
 * and BaseAuxStatus once they are merged.
 *
 * Host heartbeat: a successful agent ping (every 100 ms) counts as the host
 * heartbeat fed to the motion core and the safety core. PROPOSAL, TODO(Evans):
 * the I8 heartbeat source is not ruled; a dedicated host topic may replace it.
 */
#ifndef OAMR_UROS_APP_H
#define OAMR_UROS_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "oamr/base_app.h"
#include "oamr/clock.h"
#include "oamr/imu.h"
#include "oamr/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Each hook is called from the micro-ROS context; the board serialises
     * them against the control tick. */
    void (*on_cmd_vel)(void *ctx, float linear_m_s, float angular_rad_s);
    void (*on_host_alive)(void *ctx);
    void (*snapshot)(void *ctx, oamr_app_status_t *out);
    bool (*take_imu)(void *ctx, oamr_imu_sample_t *out);
    void *ctx;
} oamr_uros_hooks_t;

typedef struct {
    uint32_t status_period_ms;     /* 100 (I8: 10 Hz) */
    uint32_t aux_period_ms;        /* 200 (I8: 5 Hz) */
    uint32_t odom_period_ms;       /* TODO(Evans): not ruled; 20 ms (50 Hz) in SIL */
    uint32_t ping_period_ms;       /* 100 */
    uint32_t ping_timeout_ms;      /* 50 */
    uint8_t ping_misses_to_drop;   /* consecutive failed pings before reconnecting: 3 */
    const char *hardware_id;       /* DiagnosticStatus.hardware_id */
} oamr_uros_config_t;

typedef enum {
    OAMR_UROS_WAITING_AGENT = 0,
    OAMR_UROS_CONNECTED
} oamr_uros_state_t;

typedef struct {
    uint32_t sessions;             /* successful entity creations */
    uint32_t drops;                /* agent lost after a session */
    uint32_t status_published;
    uint32_t cmd_received;
} oamr_uros_counters_t;

void oamr_uros_default_config(oamr_uros_config_t *cfg);

/* Registers the transport with rmw_microxrcedds. Call once. */
void oamr_uros_init(const oamr_uros_config_t *cfg, const oamr_transport_t *transport,
                    const oamr_clock_t *clock, const oamr_uros_hooks_t *hooks);

/* One pass: ping/connect, executor spin (bounded by timeout_ms), publish what
 * is due. Never blocks longer than about timeout_ms + ping_timeout_ms. */
oamr_uros_state_t oamr_uros_spin_once(uint32_t timeout_ms);

const oamr_uros_counters_t *oamr_uros_counters(void);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_UROS_APP_H */
