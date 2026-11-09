/* SPDX-License-Identifier: MIT */
/*
 * Commissioning record: the measured and ruled values without which the base
 * must not move. The board port fills one record (see
 * boards/stm32/OpenAMRobot/Port/Inc/port_commissioning.h); until every field
 * is valid the motion core writes zero and the port keeps the safety core
 * LOCKED (reason NOT_COMMISSIONED). There are no placeholder defaults: an
 * unset float is NaN, an unset integer is 0, and both read as "missing".
 *
 * Fields and owners (all TODO(Evans) until measured or ruled):
 * - wheel_radius_m: measured 2.0 hub-motor wheel radius. P-03 item 21 says
 *   "about 0.1 m"; that is not a measurement.
 * - track_width_m: measured 2.0 track width. The roadmap model uses 0.400 m,
 *   the legacy robot measured 0.46 m; neither is the 2.0 base.
 * - accel_limit_m_s2: base acceleration limit. P-03 item 30 A6 rules only the
 *   deceleration (1.5 m/s^2 travel, 1.0 m/s^2 hold-and-move).
 * - zlac_accel_time_ms / zlac_decel_time_ms: ZLAC8015D 6083h / 6084h ramp
 *   times that give the agreed acceleration and 1.5 m/s^2 deceleration. The
 *   unit reference of the ramp (time from 0 to which speed) is in the V4.2
 *   manual, not in the inputs.
 * - the link-loss block below (decision D-24, P-03 item 30 A4).
 *
 * ZLAC8015D link loss: 2000h and the heartbeat consumer (the one place this
 * relationship is stated; the code and docs point here).
 *
 *   Two independent supervisions cover the CAN1 link, one on each side:
 *
 *   1. Drive side, object 2000h "communication power-down protection time"
 *      (ms, 0..32767, factory 0 = disabled). If the drive receives no
 *      communication for this time it stops the motors by itself. This is the
 *      only protection when the STM32 is silent (crash, IWDG reset, CAN1
 *      unplugged, bus-off). Roadmap rev 2 risk 2: 200 to 300 ms, saved with
 *      2010h, proven by unplugging CAN1 at low speed with the wheels off the
 *      ground, five times. Accepted range here: 200 to 300 ms.
 *   2. STM32 side, heartbeat consumer on the drive's producer heartbeat
 *      (1017h). Consumer time = zlac_heartbeat_misses x
 *      zlac_heartbeat_period_ms (I8 WP: 3 missed heartbeats). On loss the
 *      adapter leaves OPERATIONAL, the backend reports not ready, the motion
 *      core writes zero and requires a fresh command. Vendor rule: the
 *      consumer time must exceed 2 x 1017h, so misses must be at least 3.
 *
 *   The two times are not compared with each other in code: they guard
 *   different failures (STM32 silent vs drive silent). With 1017h = 100 ms
 *   (the first-pass proposal) the consumer time is 300 ms; with the EDS
 *   default 500 ms it is 1.5 s, which is longer than the 500 ms host
 *   heartbeat and would let the base report "ready" for up to 1.5 s after the
 *   drive died. TODO(Evans): rule 1017h and the consumer time with 2000h
 *   (D-24), and confirm on the bench which frames count as "communication"
 *   for 2000h (any frame on the bus, only frames to node 1, or only RPDO and
 *   SDO). If SDO traffic counts, the boot sequence must zero 60FFh before it
 *   reads the configuration; the adapter does that first in any case.
 */
#ifndef OAMR_COMMISSIONING_H
#define OAMR_COMMISSIONING_H

#include <stdbool.h>
#include <stdint.h>

#include "oamr/motion.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 2000h acceptance range (roadmap rev 2 risk 2). */
#define OAMR_COMM_LINK_LOSS_MIN_MS 200u
#define OAMR_COMM_LINK_LOSS_MAX_MS 300u
/* Vendor rule: consumer time > 2 x producer period. */
#define OAMR_COMM_MIN_HEARTBEAT_MISSES 3u

/* Missing-field bits. */
enum {
    OAMR_COMM_WHEEL_RADIUS = 1u << 0,
    OAMR_COMM_TRACK_WIDTH = 1u << 1,
    OAMR_COMM_ACCEL_LIMIT = 1u << 2,
    OAMR_COMM_ZLAC_RAMPS = 1u << 3,
    OAMR_COMM_ZLAC_LINK_LOSS = 1u << 4,
    OAMR_COMM_ALL = (1u << 5) - 1u
};

typedef struct {
    float wheel_radius_m;
    float track_width_m;
    float accel_limit_m_s2;
    uint32_t zlac_accel_time_ms;          /* 6083h:1/2 */
    uint32_t zlac_decel_time_ms;          /* 6084h:1/2 */
    uint16_t zlac_comm_loss_protection_ms;/* 2000h */
    uint16_t zlac_heartbeat_period_ms;    /* 1017h */
    uint8_t zlac_heartbeat_misses;        /* consumer: misses x 1017h */
} oamr_commissioning_record_t;

/* Every field unset (NaN or 0). */
void oamr_commissioning_record_init(oamr_commissioning_record_t *r);

/* Bit mask of OAMR_COMM_* fields that are missing or out of range; 0 = complete. */
uint32_t oamr_commissioning_missing(const oamr_commissioning_record_t *r);
bool oamr_commissioning_complete(const oamr_commissioning_record_t *r);

/* STM32-side heartbeat consumer time in ms (misses x 1017h). */
uint32_t oamr_commissioning_consumer_timeout_ms(const oamr_commissioning_record_t *r);

/* Copy wheel radius, track width and acceleration limit into the motion
 * configuration and set cfg->commissioned only if all three are valid.
 * Ruled values (0.5 m/s, decelerations, timeouts) are never taken from the
 * record. */
void oamr_commissioning_apply_motion(const oamr_commissioning_record_t *r,
                                     oamr_motion_config_t *cfg);

/* Software Stop as fed to the safety core: the host's Stop, or asserted for
 * as long as the record is incomplete. The safety core then stays LOCKED
 * (reason SOFTWARE_STOP), the RESET lamp stays on and a RESET press cannot
 * arm it. Note: under E-01 rev 3 a RESET press while Stop is asserted arms
 * reset_seen, so this is only correct because the record is a compile-time
 * constant in the port and never completes at runtime. */
bool oamr_commissioning_sw_stop(const oamr_commissioning_record_t *r, bool host_sw_stop);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_COMMISSIONING_H */
