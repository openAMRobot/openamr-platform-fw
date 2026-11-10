/* SPDX-License-Identifier: MIT */
/*
 * Host transport interface (byte stream). The micro-ROS custom transport
 * callbacks (open, close, write, read) map one-to-one onto this struct, so the
 * same core works over serial or USB CDC first and Ethernet UDP later
 * (platform-fw #21). Nothing here may block the control loop: the transport is
 * serviced from the background loop only.
 */
#ifndef OAMR_TRANSPORT_H
#define OAMR_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool (*open)(void *ctx);
    bool (*close)(void *ctx);
    /* Returns bytes written (may be fewer than len). */
    size_t (*write)(void *ctx, const uint8_t *buf, size_t len);
    /* Returns bytes read; waits at most timeout_ms (0 = poll). */
    size_t (*read)(void *ctx, uint8_t *buf, size_t len, uint32_t timeout_ms);
    void *ctx;
} oamr_transport_t;

#ifdef __cplusplus
}
#endif

#endif /* OAMR_TRANSPORT_H */
