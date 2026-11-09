/* SPDX-License-Identifier: MIT */
/*
 * Host board port (Linux) for the software-in-the-loop chain.
 *
 *   clock      CLOCK_MONOTONIC, microseconds
 *   CAN1/CAN2  SocketCAN raw sockets (vcan0 / vcan1 by default), error frames
 *              enabled so CAN_ERR_BUSOFF / CAN_ERR_RESTARTED drive the bus-off
 *              supervision exactly like the FDCAN PSR.BO flag on the MCU
 *   transport  serial over a pty (micro-ROS agent: `serial --dev <link>`)
 *   IMU        fake ICM-42688-P register file behind oamr_spi_t (host_imu.c)
 *   safety     GPIO inputs from a small text control socket (host_ctl.c);
 *              boot values are the unsafe ones, so the core stays LOCKED
 *
 * Nothing in this directory is built for the STM32.
 */
#ifndef OAMR_HOST_BOARD_H
#define OAMR_HOST_BOARD_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "oamr/base_app.h"
#include "oamr/clock.h"
#include "oamr/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

uint64_t host_now_us(void *ctx);

/* --- SocketCAN ------------------------------------------------------------ */
typedef struct {
    int fd;
    char ifname[32];
    bool bus_off;          /* from CAN_ERR_BUSOFF until CAN_ERR_RESTARTED / restart */
    bool link_up;          /* interface exists and is up */
    uint32_t tx_errors;
    uint32_t rx_frames;
} host_can_t;

bool host_can_open(host_can_t *c, const char *ifname);
void host_can_close(host_can_t *c);
/* Non-blocking: returns true and fills f for a data frame; error frames
 * update bus_off and return false with *got_error set. */
bool host_can_recv(host_can_t *c, oamr_can_frame_t *f, bool *got_error);
oamr_can_tx_t host_can_tx(host_can_t *c);
/* Controller restart request from the bus-off supervision. On vcan the
 * restart succeeds once the injected bus-off has been cleared by the
 * emulator's CAN_ERR_RESTARTED frame; a missing interface stays bus-off. */
void host_can_restart(host_can_t *c);

/* --- pty transport --------------------------------------------------------- */
typedef struct {
    int master_fd;
    char slave_path[64];
    char link_path[256];   /* stable symlink for the agent, may be empty */
} host_pty_t;

bool host_pty_open(host_pty_t *p, const char *link_path);
void host_pty_close(host_pty_t *p);
oamr_transport_t host_pty_transport(host_pty_t *p);

/* --- fake IMU ---------------------------------------------------------------- */
typedef struct {
    uint8_t regs[128];
    bool spi_fail;
    bool inject_invalid;   /* next samples carry the -32768 invalid code */
    bool absent;           /* WHO_AM_I reads 0 (not present) */
    uint64_t next_sample_us;
    uint32_t period_us;    /* 5000 (200 Hz ODR) */
} host_imu_t;

void host_imu_init(host_imu_t *imu);
oamr_spi_t host_imu_spi(host_imu_t *imu);
/* Produce a data-ready sample when one is due (called from the background loop). */
void host_imu_step(host_imu_t *imu, uint64_t now_us);

/* --- control socket ------------------------------------------------------------- */
typedef struct {
    pthread_mutex_t *lock;
    oamr_app_gpio_t *gpio;
    host_imu_t *imu;
    oamr_app_t *app;
    bool *test_override;
    bool *quit;
    int listen_fd;
    int client_fd;
    char buf[256];
    size_t len;
} host_ctl_t;

bool host_ctl_open(host_ctl_t *c, const char *path);
/* Accept and process pending commands without blocking. */
void host_ctl_poll(host_ctl_t *c);
void host_ctl_close(host_ctl_t *c, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_HOST_BOARD_H */
