/* SPDX-License-Identifier: MIT */
/*
 * Host board port main: the base controller as a Linux process.
 *
 *   control thread  1 kHz tick (absolute-time sleep), owns CAN RX, the app
 *                   tick, bus-off restarts and the IMU poll; the MCU's timer
 *                   interrupt + background loop
 *   micro-ROS       oamr_uros_spin_once() in its own thread (lower priority
 *                   on the MCU); talks to the app only through the hooks,
 *                   under the same mutex
 *   main thread     test control socket and signal handling
 *
 * Usage: oamr_base_host [--can1 vcan0] [--can2 vcan1] [--pty /tmp/oamr_base_tty]
 *                       [--ctl /tmp/oamr_base.ctl] [--commissioning FILE]
 *                       [--sil-safety-override]
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "host_board.h"
#include "uros_app.h"

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static oamr_app_t s_app;
static oamr_app_gpio_t s_gpio;
static host_can_t s_can1, s_can2;
static host_imu_t s_imu;
static host_pty_t s_pty;
static bool s_override;
static volatile bool s_quit;
static bool s_quit_req;
static int s_exit_code;

uint64_t host_now_us(void *ctx)
{
    (void)ctx;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void on_signal(int sig)
{
    (void)sig;
    s_quit = true;
}

/* --- commissioning record from a key=value file (SIL only) ---------------- */
static bool load_commissioning(const char *path, oamr_commissioning_record_t *r)
{
    oamr_commissioning_record_init(r);
    if (path == NULL) {
        return true;  /* empty record: NOT_COMMISSIONED, as on the STM32 today */
    }
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        return false;
    }
    char line[160];
    while (fgets(line, sizeof(line), f) != NULL) {
        char key[64];
        double v;
        if (line[0] == '#' || sscanf(line, " %63[a-z_0-9] = %lf", key, &v) != 2) {
            continue;
        }
        if (!strcmp(key, "wheel_radius_m")) r->wheel_radius_m = (float)v;
        else if (!strcmp(key, "track_width_m")) r->track_width_m = (float)v;
        else if (!strcmp(key, "accel_limit_m_s2")) r->accel_limit_m_s2 = (float)v;
        else if (!strcmp(key, "zlac_accel_time_ms")) r->zlac_accel_time_ms = (uint32_t)v;
        else if (!strcmp(key, "zlac_decel_time_ms")) r->zlac_decel_time_ms = (uint32_t)v;
        else if (!strcmp(key, "zlac_comm_loss_protection_ms")) r->zlac_comm_loss_protection_ms = (uint16_t)v;
        else if (!strcmp(key, "zlac_heartbeat_period_ms")) r->zlac_heartbeat_period_ms = (uint16_t)v;
        else if (!strcmp(key, "zlac_heartbeat_misses")) r->zlac_heartbeat_misses = (uint8_t)v;
        else fprintf(stderr, "commissioning: unknown key %s\n", key);
    }
    fclose(f);
    return true;
}

/* --- micro-ROS hooks ------------------------------------------------------- */
static void hook_cmd(void *ctx, float v, float w)
{
    (void)ctx;
    pthread_mutex_lock(&s_lock);
    oamr_app_cmd_vel(&s_app, v, w, host_now_us(NULL));
    pthread_mutex_unlock(&s_lock);
}

static void hook_alive(void *ctx)
{
    (void)ctx;
    pthread_mutex_lock(&s_lock);
    oamr_app_host_heartbeat(&s_app, host_now_us(NULL));
    pthread_mutex_unlock(&s_lock);
}

static void hook_snapshot(void *ctx, oamr_app_status_t *out)
{
    (void)ctx;
    pthread_mutex_lock(&s_lock);
    oamr_app_snapshot(&s_app, host_now_us(NULL), out);
    pthread_mutex_unlock(&s_lock);
}

static bool hook_imu(void *ctx, oamr_imu_sample_t *out)
{
    (void)ctx;
    pthread_mutex_lock(&s_lock);
    const bool ok = oamr_app_take_imu_sample(&s_app, out);
    pthread_mutex_unlock(&s_lock);
    return ok;
}

static void *uros_thread(void *arg)
{
    (void)arg;
    while (!s_quit) {
        if (oamr_uros_spin_once(5) == OAMR_UROS_WAITING_AGENT) {
            usleep(10000);
        }
    }
    return NULL;
}

/* --- control thread --------------------------------------------------------- */
static void drain_can(host_can_t *c, bool is_can1, uint64_t now_us)
{
    oamr_can_frame_t f;
    bool err;
    for (int i = 0; i < 64; i++) {
        const bool got = host_can_recv(c, &f, &err);
        if (!got && !err) {
            break;
        }
        if (got) {
            if (is_can1) {
                oamr_app_on_can1_frame(&s_app, &f, now_us);
            } else {
                oamr_app_on_can2_frame(&s_app, &f, now_us);
            }
        }
    }
}

static void *control_thread(void *arg)
{
    (void)arg;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    uint64_t last_reattach = 0;
    while (!s_quit) {
        next.tv_nsec += 1000000L;
        if (next.tv_nsec >= 1000000000L) {
            next.tv_nsec -= 1000000000L;
            next.tv_sec++;
        }
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL) == EINTR) {
        }
        pthread_mutex_lock(&s_lock);
        const uint64_t now = host_now_us(NULL);
        drain_can(&s_can1, true, now);
        drain_can(&s_can2, false, now);
        /* A removed interface is not bus-off; re-attach when it returns. */
        if (now - last_reattach > 500000u) {
            last_reattach = now;
            if (s_can1.fd < 0) host_can_restart(&s_can1);
            if (s_can2.fd < 0) host_can_restart(&s_can2);
        }
        if (s_override && s_gpio.estop_column != OAMR_SC_COLUMN_UNKNOWN) {
            /* CAN3 (CTL-017) is not part of this chain: the override stands in
             * for its 2nd E-stop observation. */
            s_gpio.estop_column_seen = true;
            s_gpio.estop_column_rx_us = now;
        }
        const oamr_app_actions_t a =
            oamr_app_control_tick(&s_app, now, &s_gpio, s_can1.bus_off, s_can2.bus_off);
        if (a.restart_can1) host_can_restart(&s_can1);
        if (a.restart_can2) host_can_restart(&s_can2);
        host_imu_step(&s_imu, now);
        oamr_app_background(&s_app, now);
        if (a.controller_reset) {
            /* The MCU resets here; the SIL process exits and the harness restarts it. */
            fprintf(stderr, "controller reset requested by the safety core\n");
            s_exit_code = 3;
            s_quit = true;
        }
        pthread_mutex_unlock(&s_lock);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    const char *can1 = "vcan0", *can2 = "vcan1", *pty = "/tmp/oamr_base_tty";
    const char *ctl = "/tmp/oamr_base.ctl", *comm = NULL;
    for (int i = 1; i < argc; i++) {
        const bool has = i + 1 < argc;
        if (!strcmp(argv[i], "--can1") && has) can1 = argv[++i];
        else if (!strcmp(argv[i], "--can2") && has) can2 = argv[++i];
        else if (!strcmp(argv[i], "--pty") && has) pty = argv[++i];
        else if (!strcmp(argv[i], "--ctl") && has) ctl = argv[++i];
        else if (!strcmp(argv[i], "--commissioning") && has) comm = argv[++i];
        else if (!strcmp(argv[i], "--sil-safety-override")) s_override = true;
        else {
            fprintf(stderr, "usage: %s [--can1 IF] [--can2 IF] [--pty LINK] [--ctl SOCK] "
                            "[--commissioning FILE] [--sil-safety-override]\n", argv[0]);
            return 2;
        }
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    oamr_app_board_t board;
    memset(&board, 0, sizeof(board));
    if (!load_commissioning(comm, &board.commissioning)) {
        return 2;
    }
    if (!host_can_open(&s_can1, can1)) fprintf(stderr, "warning: %s not available\n", can1);
    if (!host_can_open(&s_can2, can2)) fprintf(stderr, "warning: %s not available\n", can2);
    if (!host_pty_open(&s_pty, pty)) {
        fprintf(stderr, "cannot create the pty link %s\n", pty);
        return 2;
    }
    host_imu_init(&s_imu);
    board.can1 = host_can_tx(&s_can1);
    board.can2 = host_can_tx(&s_can2);
    board.imu_spi = host_imu_spi(&s_imu);
    board.boot_id = (uint32_t)(host_now_us(NULL) ^ ((uint64_t)getpid() << 16));
    board.reset_cause = OAMR_RESET_POWER_ON;
    board.now_us = host_now_us;

    /* Safety inputs: invalid (unassigned pins) unless the SIL override is on;
     * with the override they start in their unsafe values. */
    memset(&s_gpio, 0, sizeof(s_gpio));
    s_gpio.gpio_valid = s_override;
    s_gpio.estop_column = OAMR_SC_COLUMN_UNKNOWN;

    oamr_app_init(&s_app, &board, host_now_us(NULL));

    oamr_uros_config_t ucfg;
    oamr_uros_default_config(&ucfg);
    const oamr_transport_t tr = host_pty_transport(&s_pty);
    const oamr_clock_t clk = {host_now_us, NULL};
    const oamr_uros_hooks_t hooks = {hook_cmd, hook_alive, hook_snapshot, hook_imu, NULL};
    oamr_uros_init(&ucfg, &tr, &clk, &hooks);

    host_ctl_t hc;
    memset(&hc, 0, sizeof(hc));
    hc.lock = &s_lock;
    hc.gpio = &s_gpio;
    hc.imu = &s_imu;
    hc.app = &s_app;
    hc.test_override = &s_override;
    hc.quit = &s_quit_req;
    if (!host_ctl_open(&hc, ctl)) fprintf(stderr, "warning: control socket %s not available\n", ctl);

    printf("oamr_base_host: pty %s -> %s, can1 %s, can2 %s, ctl %s, commissioning %s%s\n",
           pty, s_pty.slave_path, can1, can2, ctl, comm ? comm : "(none)",
           s_override ? ", SIL SAFETY OVERRIDE" : "");
    fflush(stdout);

    pthread_t tc, tu;
    pthread_create(&tc, NULL, control_thread, NULL);
    pthread_create(&tu, NULL, uros_thread, NULL);
    while (!s_quit) {
        pthread_mutex_lock(&s_lock);
        host_ctl_poll(&hc);
        pthread_mutex_unlock(&s_lock);
        if (s_quit_req) s_quit = true;
        usleep(2000);
    }
    pthread_join(tc, NULL);
    pthread_join(tu, NULL);
    host_ctl_close(&hc, ctl);
    host_pty_close(&s_pty);
    host_can_close(&s_can1);
    host_can_close(&s_can2);
    return s_exit_code;
}
