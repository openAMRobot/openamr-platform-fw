/* SPDX-License-Identifier: MIT */
/*
 * Serial transport over a pseudo-terminal: the base controller owns the
 * master side; the micro-ROS agent opens the slave (`serial --dev <link>`),
 * the same as a USB CDC or UART link on the bench (issue #21).
 */
#define _GNU_SOURCE
#include "host_board.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

bool host_pty_open(host_pty_t *p, const char *link_path)
{
    memset(p, 0, sizeof(*p));
    p->master_fd = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (p->master_fd < 0 || grantpt(p->master_fd) != 0 || unlockpt(p->master_fd) != 0) {
        return false;
    }
    const char *name = ptsname(p->master_fd);
    if (name == NULL) {
        return false;
    }
    snprintf(p->slave_path, sizeof(p->slave_path), "%s", name);
    /* Raw mode on the line discipline: the XRCE-DDS serial framing is binary. */
    const int s = open(p->slave_path, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (s >= 0) {
        struct termios tio;
        if (tcgetattr(s, &tio) == 0) {
            cfmakeraw(&tio);
            (void)tcsetattr(s, TCSANOW, &tio);
        }
        close(s);
    }
    if (link_path != NULL && link_path[0] != '\0') {
        snprintf(p->link_path, sizeof(p->link_path), "%s", link_path);
        (void)unlink(p->link_path);
        if (symlink(p->slave_path, p->link_path) != 0) {
            return false;
        }
    }
    return true;
}

void host_pty_close(host_pty_t *p)
{
    if (p->link_path[0] != '\0') {
        (void)unlink(p->link_path);
    }
    if (p->master_fd >= 0) {
        close(p->master_fd);
    }
    p->master_fd = -1;
}

static size_t pty_write(void *ctx, const uint8_t *buf, size_t len)
{
    host_pty_t *p = (host_pty_t *)ctx;
    size_t done = 0;
    while (done < len) {
        const ssize_t n = write(p->master_fd, buf + done, len - done);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN) {
                struct pollfd pf = {p->master_fd, POLLOUT, 0};
                if (poll(&pf, 1, 10) <= 0) {
                    break;
                }
                continue;
            }
            break;
        }
        done += (size_t)n;
    }
    return done;
}

static size_t pty_read(void *ctx, uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    host_pty_t *p = (host_pty_t *)ctx;
    struct pollfd pf = {p->master_fd, POLLIN, 0};
    const int r = poll(&pf, 1, (int)timeout_ms);
    if (r <= 0 || (pf.revents & POLLIN) == 0) {
        if (pf.revents & POLLHUP) {
            /* No agent on the slave side: behave like a silent line. */
            usleep(timeout_ms * 1000u);
        }
        return 0;
    }
    const ssize_t n = read(p->master_fd, buf, len);
    return n > 0 ? (size_t)n : 0u;
}

oamr_transport_t host_pty_transport(host_pty_t *p)
{
    oamr_transport_t t = {NULL, NULL, pty_write, pty_read, p};
    return t;
}
