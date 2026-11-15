/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
#ifndef SIMREF_CAN_H
#define SIMREF_CAN_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { SIMREF_BUS_DRIVE = 0, SIMREF_BUS_AUX, SIMREF_BUS_COUNT } simref_bus_t;

typedef struct {
    uint32_t id;     /* 11-bit identifier */
    uint8_t len;
    uint8_t data[8];
} simref_frame_t;

typedef void (*simref_send_fn)(void *ctx, simref_bus_t bus, const simref_frame_t *f);

/* Interface names the process may open: only names starting with "vcan". */
bool simref_iface_allowed(const char *name);

#endif
