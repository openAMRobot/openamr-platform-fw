/* SPDX-License-Identifier: MIT */
/* Size probe, NOT FIRMWARE: transport hooks for the micro-ROS custom transport. */
#ifndef PROBE_TRANSPORT_H
#define PROBE_TRANSPORT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <uxr/client/transport.h>

#if defined(PROBE_TRANSPORT_SERIAL)
#define PROBE_TRANSPORT_FRAMING true   /* HDLC-like framing for a byte stream */
#else
#define PROBE_TRANSPORT_FRAMING false  /* one datagram per XRCE message */
#endif

bool probe_transport_open(struct uxrCustomTransport *t);
bool probe_transport_close(struct uxrCustomTransport *t);
size_t probe_transport_write(struct uxrCustomTransport *t, const uint8_t *buf, size_t len, uint8_t *err);
size_t probe_transport_read(struct uxrCustomTransport *t, uint8_t *buf, size_t len, int timeout, uint8_t *err);
void probe_set_allocator(void);
void probe_idle(void);
#endif
