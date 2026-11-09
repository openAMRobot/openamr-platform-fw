/* SPDX-License-Identifier: MIT */
/*
 * Test control socket of the host port (Unix stream socket, one line per
 * command, one line per reply). This is the SIL stand-in for the hardware
 * the bench cannot provide: safety GPIO, the RESET button, the software Stop
 * and IMU faults.
 *
 * TEST ONLY. The safety GPIO commands work only when the binary was started
 * with --sil-safety-override; otherwise the safety inputs stay invalid
 * (gpio_valid false, as on the STM32 while the pins are TODO(Evans)) and the
 * core stays LOCKED. This file is part of boards/host only and refuses to
 * compile for an ARM target (see the #error below).
 *
 * Commands:
 *   safety_ok 0|1     estop 0|1     reset 0|1     column released|pressed|unknown
 *   sw_stop 0|1       charge 0|1
 *   imu invalid 0|1   imu spi_fail 0|1
 *   state             (one line of key=value pairs)
 *   quit
 */
#if defined(__arm__) || defined(__ARM_ARCH) || defined(__thumb__) || defined(STM32H723xx)
#error "host_ctl.c is the SIL test override; it must never be built for the MCU"
#endif

#define _GNU_SOURCE
#include "host_board.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

bool host_ctl_open(host_ctl_t *c, const char *path)
{
    c->listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    c->client_fd = -1;
    c->len = 0;
    if (c->listen_fd < 0) {
        return false;
    }
    struct sockaddr_un a;
    memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof(a.sun_path), "%s", path);
    (void)unlink(path);
    if (bind(c->listen_fd, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(c->listen_fd, 2) != 0) {
        close(c->listen_fd);
        c->listen_fd = -1;
        return false;
    }
    return true;
}

void host_ctl_close(host_ctl_t *c, const char *path)
{
    if (c->client_fd >= 0) {
        close(c->client_fd);
    }
    if (c->listen_fd >= 0) {
        close(c->listen_fd);
        (void)unlink(path);
    }
}

static void reply(host_ctl_t *c, const char *s)
{
    if (c->client_fd >= 0) {
        (void)!write(c->client_fd, s, strlen(s));
    }
}

static void state_line(host_ctl_t *c)
{
    oamr_app_status_t s;
    oamr_app_snapshot(c->app, host_now_us(NULL), &s);
    char out[512];
    snprintf(out, sizeof(out),
             "safety=%s motion_allowed=%d applied=%s left_m_s=%.4f right_m_s=%.4f drive=%d "
             "faults=%u reasons=0x%03x override=%d\n",
             oamr_app_safety_state_name(s.safety_state), s.motion_allowed ? 1 : 0,
             oamr_app_applied_state_name(s.applied.state), (double)s.applied.left_m_s,
             (double)s.applied.right_m_s, (int)s.drive_state, (unsigned)s.n_faults,
             (unsigned)s.safety_reasons, *c->test_override ? 1 : 0);
    reply(c, out);
}

static void handle(host_ctl_t *c, char *line)
{
    char cmd[32] = {0};
    char arg[32] = {0};
    char arg2[32] = {0};
    const int n = sscanf(line, "%31s %31s %31s", cmd, arg, arg2);
    if (n < 1) {
        return;
    }
    const bool on = strcmp(arg, "1") == 0;
    const bool gpio_cmd = strcmp(cmd, "safety_ok") == 0 || strcmp(cmd, "estop") == 0 ||
                          strcmp(cmd, "reset") == 0 || strcmp(cmd, "column") == 0;
    if (gpio_cmd && !*c->test_override) {
        reply(c, "error safety override not enabled (--sil-safety-override)\n");
        return;
    }
    if (strcmp(cmd, "safety_ok") == 0) {
        c->gpio->safety_ok = on;
    } else if (strcmp(cmd, "estop") == 0) {
        c->gpio->estop_base_active = on;
    } else if (strcmp(cmd, "reset") == 0) {
        c->gpio->reset_pressed = on;
    } else if (strcmp(cmd, "column") == 0) {
        c->gpio->estop_column = strcmp(arg, "released") == 0  ? OAMR_SC_COLUMN_RELEASED
                                : strcmp(arg, "pressed") == 0 ? OAMR_SC_COLUMN_PRESSED
                                                              : OAMR_SC_COLUMN_UNKNOWN;
    } else if (strcmp(cmd, "sw_stop") == 0) {
        oamr_app_sw_stop(c->app, on);
    } else if (strcmp(cmd, "charge") == 0) {
        oamr_app_charge_cmd(c->app, on);
    } else if (strcmp(cmd, "imu") == 0) {
        const bool v = strcmp(arg2, "1") == 0;
        if (strcmp(arg, "invalid") == 0) {
            c->imu->inject_invalid = v;
        } else if (strcmp(arg, "spi_fail") == 0) {
            c->imu->spi_fail = v;
        } else {
            reply(c, "error imu\n");
            return;
        }
    } else if (strcmp(cmd, "state") == 0) {
        state_line(c);
        return;
    } else if (strcmp(cmd, "quit") == 0) {
        *c->quit = true;
    } else {
        reply(c, "error unknown command\n");
        return;
    }
    reply(c, "ok\n");
}

void host_ctl_poll(host_ctl_t *c)
{
    if (c->listen_fd < 0) {
        return;
    }
    if (c->client_fd < 0) {
        c->client_fd = accept4(c->listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        c->len = 0;
        if (c->client_fd < 0) {
            return;
        }
    }
    const ssize_t r = read(c->client_fd, c->buf + c->len, sizeof(c->buf) - 1u - c->len);
    if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        close(c->client_fd);
        c->client_fd = -1;
        return;
    }
    if (r > 0) {
        c->len += (size_t)r;
    }
    c->buf[c->len] = '\0';
    char *nl;
    while ((nl = strchr(c->buf, '\n')) != NULL) {
        *nl = '\0';
        handle(c, c->buf);
        const size_t used = (size_t)(nl - c->buf) + 1u;
        memmove(c->buf, nl + 1, c->len - used + 1u);
        c->len -= used;
    }
    if (c->len >= sizeof(c->buf) - 1u) {
        c->len = 0;  /* overlong line: drop */
    }
}
