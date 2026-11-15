/* SPDX-License-Identifier: MIT */
/*
 * The one transport interface of the micro-ROS client. tlm_uros.c talks to
 * the agent only through this structure; the publishers and the state machine
 * know nothing about the link.
 *
 * Implementations:
 *   tlm_uros_transport_serial.c  USART3 (the NUCLEO ST-LINK virtual COM port),
 *                                framed XRCE stream. SIMULATION AND BENCH ONLY:
 *                                Renode and a bench PC with a serial agent.
 *   (not written yet)            UDP over Ethernet is the robot target (Jetson
 *                                agent); it implements this same structure with
 *                                framing = false and replaces the serial file in
 *                                the build. Publishers and state machine stay.
 */
#ifndef TLM_UROS_TRANSPORT_H
#define TLM_UROS_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    const char *name;  /* for the status message: "serial_usart3", later "udp" */
    bool framing;      /* XRCE stream framing: true for a byte stream, false for datagrams */
    bool (*open)(void);
    bool (*close)(void);
    /* Bytes written; *err non-zero on failure. Must return within the transport's own bound. */
    size_t (*write)(const uint8_t *buf, size_t len, uint8_t *err);
    /* Bytes read, at most len, waiting at most timeout_ms for the first byte. */
    size_t (*read)(uint8_t *buf, size_t len, int timeout_ms, uint8_t *err);
} tlm_uros_transport_t;

/* The transport this build links. */
const tlm_uros_transport_t *tlm_uros_transport(void);

#endif /* TLM_UROS_TRANSPORT_H */
