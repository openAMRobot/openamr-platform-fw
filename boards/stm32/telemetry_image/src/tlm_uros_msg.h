/* SPDX-License-Identifier: MIT */
/*
 * Values the micro-ROS client publishes, taken from the status snapshot the
 * console uses (tlm_status_t). Pure C: tlm_uros.c copies them into the ROS
 * messages; the host tests check the conversion.
 *
 * Rule: a value that is not known is NaN, never 0. A wheel speed is a number
 * only when the image reports that channel "valid"; a range only when the
 * TF-Luna sample is valid. Topics (Proposed, see README):
 *   wheel_speeds     std_msgs/Float32MultiArray [ch1, ch2], drive units of
 *                    606Ch:01 and :02 (the unit is not settled: zlac_telemetry.h)
 *   drive_heartbeat  std_msgs/String, the heartbeat state of the drive
 *   tof/front        sensor_msgs/Range, TF-Luna on USART2, frame tof_front
 *   tof/rear         sensor_msgs/Range, TF-Luna on USART6, frame tof_rear
 *   status           std_msgs/String, one JSON object: boot id, boot record,
 *                    sequence, uptime, build, client state, missed deadlines,
 *                    CAN1 requests sent, CAN2/CAN3 frames received, lift, BMS
 */
#ifndef TLM_UROS_MSG_H
#define TLM_UROS_MSG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tlm_status.h"

/* sensor_msgs/Range limits of the TF-Luna. Source: openamr-platform-sw
 * openamrobot_description sensors.xacro ("TF-Luna: 2 deg field of view, 0.2 to
 * 8 m"), from the Benewake TF-Luna datasheet. */
#define TLM_UROS_TFLUNA_FOV_RAD 0.0349066f
#define TLM_UROS_TFLUNA_MIN_M 0.2f
#define TLM_UROS_TFLUNA_MAX_M 8.0f
/* Status JSON buffer. A best-effort message must fit one XRCE transport MTU
 * (512 bytes, Micro-XRCE-DDS-Client default) with its headers. */
#define TLM_UROS_STATUS_MAX 440u

typedef struct
{
    float wheel[2];          /* ch1, ch2 in drive units, NaN unless valid */
    const char *drive_hb;    /* disabled, waiting, alive, lost */
    float range_m[TLM_N_TFLUNA]; /* front (usart2), rear (usart6); NaN unless valid */
    uint32_t stamp_sec;      /* uptime of the snapshot: the image has no ROS clock */
    uint32_t stamp_nanosec;
    char status[TLM_UROS_STATUS_MAX];
    size_t status_len;       /* 0 when the JSON did not fit (never sent cut) */
} tlm_uros_values_t;

typedef struct
{
    const char *commit;
    const char *client_state;
    uint32_t sessions;
    uint32_t losses;
    uint32_t sens_missed;    /* sensors task deadline misses */
    uint32_t arena_high_water; /* bytes of the static arena ever used */
    uint32_t stack_free_words; /* reporter (micro-ROS) task stack never used */
} tlm_uros_meta_t;

void tlm_uros_values(const tlm_status_t *s, const tlm_uros_meta_t *meta, tlm_uros_values_t *out);

#endif /* TLM_UROS_MSG_H */
