/* SPDX-License-Identifier: MIT */
/*
 * Commissioning record for this robot (see firmware/core/include/oamr/commissioning.h
 * for the meaning of every field and the 2000h / heartbeat-consumer rule).
 *
 * Every field is unset on purpose. While any field is unset the board:
 * - keeps the safety core LOCKED (Software Stop asserted, RESET lamp on),
 * - writes zero wheel velocity (motion core NOT_COMMISSIONED),
 * - while a ZLAC field (ramps or link loss) is unset, does not start the
 *   ZLAC8015D adapter, so it sends nothing on CAN1 and never writes or saves
 *   (2010h) any drive object. With the ZLAC fields set and the geometry
 *   still unset, the drive is configured and supervised but the base cannot
 *   move (wheels-up D-24 commissioning).
 * Fill a field only with a measured value or an owner ruling, and record its
 * source in the commit message.
 */
#ifndef PORT_COMMISSIONING_H
#define PORT_COMMISSIONING_H

#include <math.h>

#include "oamr/commissioning.h"

/* TODO(Evans): measured 2.0 wheel radius, metres. */
#define PORT_COMM_WHEEL_RADIUS_M NAN
/* TODO(Evans): measured 2.0 track width, metres (model 0.400, legacy 0.46: neither applies). */
#define PORT_COMM_TRACK_WIDTH_M NAN
/* TODO(Evans): base acceleration limit, m/s^2 (only deceleration is ruled, P-03 item 30 A6). */
#define PORT_COMM_ACCEL_LIMIT_M_S2 NAN
/* TODO(Evans): ZLAC8015D 6083h / 6084h ramp times, ms (V4.2 manual unit reference). */
#define PORT_COMM_ZLAC_ACCEL_TIME_MS 0u
#define PORT_COMM_ZLAC_DECEL_TIME_MS 0u
/* TODO(Evans): D-24 ruling. 2000h in 200..300 ms; 1017h producer period;
 * consumer misses >= 3. First-pass proposal: 250 ms, 100 ms, 3. */
#define PORT_COMM_ZLAC_LINK_LOSS_MS 0u
#define PORT_COMM_ZLAC_HEARTBEAT_MS 0u
#define PORT_COMM_ZLAC_HEARTBEAT_MISSES 0u

static inline oamr_commissioning_record_t port_commissioning_record(void)
{
    oamr_commissioning_record_t r;
    oamr_commissioning_record_init(&r);
    r.wheel_radius_m = PORT_COMM_WHEEL_RADIUS_M;
    r.track_width_m = PORT_COMM_TRACK_WIDTH_M;
    r.accel_limit_m_s2 = PORT_COMM_ACCEL_LIMIT_M_S2;
    r.zlac_accel_time_ms = PORT_COMM_ZLAC_ACCEL_TIME_MS;
    r.zlac_decel_time_ms = PORT_COMM_ZLAC_DECEL_TIME_MS;
    r.zlac_comm_loss_protection_ms = PORT_COMM_ZLAC_LINK_LOSS_MS;
    r.zlac_heartbeat_period_ms = PORT_COMM_ZLAC_HEARTBEAT_MS;
    r.zlac_heartbeat_misses = PORT_COMM_ZLAC_HEARTBEAT_MISSES;
    return r;
}

#endif /* PORT_COMMISSIONING_H */
