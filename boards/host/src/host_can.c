/* SPDX-License-Identifier: MIT */
/* SocketCAN backend for the host board port. See host_board.h. */
#define _GNU_SOURCE
#include "host_board.h"

#include <errno.h>
#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static bool iface_up(const char *ifname)
{
    char path[96];
    snprintf(path, sizeof(path), "/sys/class/net/%s/flags", ifname);
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }
    unsigned flags = 0;
    const int n = fscanf(f, "%x", &flags);
    fclose(f);
    return n == 1 && (flags & IFF_UP) != 0u;
}

static bool open_socket(host_can_t *c)
{
    const int fd = socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, CAN_RAW);
    if (fd < 0) {
        return false;
    }
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%.15s", c->ifname);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        close(fd);
        return false;
    }
    const can_err_mask_t mask = CAN_ERR_BUSOFF | CAN_ERR_RESTARTED | CAN_ERR_CRTL;
    (void)setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &mask, sizeof(mask));
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return false;
    }
    c->fd = fd;
    return true;
}

bool host_can_open(host_can_t *c, const char *ifname)
{
    memset(c, 0, sizeof(*c));
    c->fd = -1;
    snprintf(c->ifname, sizeof(c->ifname), "%s", ifname);
    c->link_up = open_socket(c) && iface_up(ifname);
    return c->fd >= 0;
}

void host_can_close(host_can_t *c)
{
    if (c->fd >= 0) {
        close(c->fd);
    }
    c->fd = -1;
}

static void lost(host_can_t *c)
{
    /* Interface vanished (ENODEV/ENETDOWN): the controller sees no bus. On
     * the MCU this is a cable or transceiver loss, which shows as missing
     * heartbeats, not as bus-off; keep it that way here. */
    host_can_close(c);
    c->link_up = false;
}

bool host_can_recv(host_can_t *c, oamr_can_frame_t *f, bool *got_error)
{
    *got_error = false;
    if (c->fd < 0) {
        return false;
    }
    struct can_frame cf;
    const ssize_t n = read(c->fd, &cf, sizeof(cf));
    if (n < 0) {
        if (errno == ENODEV || errno == ENETDOWN) {
            lost(c);
        }
        return false;
    }
    if (n != (ssize_t)sizeof(cf)) {
        return false;
    }
    if (cf.can_id & CAN_ERR_FLAG) {
        *got_error = true;
        if (cf.can_id & CAN_ERR_BUSOFF) {
            c->bus_off = true;
        }
        if (cf.can_id & CAN_ERR_RESTARTED) {
            c->bus_off = false;
        }
        return false;
    }
    if (c->bus_off) {
        return false;  /* a bus-off controller receives nothing */
    }
    if (cf.can_id & CAN_RTR_FLAG) {
        return false;
    }
    f->extended = (cf.can_id & CAN_EFF_FLAG) != 0u;
    f->id = cf.can_id & (f->extended ? CAN_EFF_MASK : CAN_SFF_MASK);
    f->dlc = cf.can_dlc > 8 ? 8 : cf.can_dlc;
    memcpy(f->data, cf.data, 8);
    c->rx_frames++;
    return true;
}

static bool can_send(void *ctx, const oamr_can_frame_t *f)
{
    host_can_t *c = (host_can_t *)ctx;
    if (c->fd < 0 || c->bus_off) {
        c->tx_errors++;
        return false;
    }
    struct can_frame cf;
    memset(&cf, 0, sizeof(cf));
    cf.can_id = f->extended ? ((f->id & CAN_EFF_MASK) | CAN_EFF_FLAG) : (f->id & CAN_SFF_MASK);
    cf.can_dlc = f->dlc > 8 ? 8 : f->dlc;
    memcpy(cf.data, f->data, 8);
    if (write(c->fd, &cf, sizeof(cf)) != (ssize_t)sizeof(cf)) {
        if (errno == ENODEV || errno == ENETDOWN) {
            lost(c);
        }
        c->tx_errors++;
        return false;
    }
    return true;
}

oamr_can_tx_t host_can_tx(host_can_t *c)
{
    oamr_can_tx_t tx = {can_send, c};
    return tx;
}

void host_can_restart(host_can_t *c)
{
    /* Re-attach when the interface came back (a recreated vcan has a new
     * index). Bus-off itself clears only on CAN_ERR_RESTARTED. */
    if (c->fd < 0 || !c->link_up) {
        host_can_close(c);
        c->link_up = open_socket(c) && iface_up(c->ifname);
    }
}
