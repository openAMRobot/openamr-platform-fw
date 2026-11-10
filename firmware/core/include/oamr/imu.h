/* SPDX-License-Identifier: MIT */
/*
 * IMU sample as seen by the core (I8 baseline: IMU by interrupt, invalid
 * samples are never published as data, they are counted and reported).
 *
 * A sample carries its own validity and its timestamp; age is computed by the
 * consumer from the core clock. A producer never hands out the same sample
 * twice and never marks an old measurement as a new valid sample.
 */
#ifndef OAMR_IMU_H
#define OAMR_IMU_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OAMR_IMU_STAMP_INT1 = 0, /* timestamp taken at the INT1 data-ready edge */
    OAMR_IMU_STAMP_POLL = 1  /* timestamp taken when polling found data ready */
} oamr_imu_stamp_source_t;

typedef struct {
    bool valid;
    uint32_t seq;               /* increments for every attempted read */
    uint64_t stamp_us;          /* core clock */
    oamr_imu_stamp_source_t stamp_source;
    float gyro_rad_s[3];        /* sensor frame */
    float accel_m_s2[3];        /* sensor frame */
    float temperature_c;
} oamr_imu_sample_t;

typedef struct {
    uint32_t samples_ok;
    uint32_t read_failures;     /* bus transfer failed */
    uint32_t invalid_samples;   /* sensor returned its invalid code */
    uint32_t consecutive_bad;
} oamr_imu_counters_t;

static inline uint64_t oamr_imu_sample_age_us(const oamr_imu_sample_t *s, uint64_t now_us)
{
    return (now_us >= s->stamp_us) ? (now_us - s->stamp_us) : 0u;
}

#ifdef __cplusplus
}
#endif

#endif /* OAMR_IMU_H */
