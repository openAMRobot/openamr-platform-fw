/* SPDX-License-Identifier: MIT */
/* Key table of the SIL chain. Every key must be set by the scenario; there is
 * no default anywhere. Values marked TODO(Evans) in the scenario files are
 * emulator conventions or proposals, not drive or robot facts. */
#include <string.h>

#include "chain.h"

static const char *const KEY_NAMES[K_COUNT] = {
    [K_NODE_ID] = "node_id",
    [K_SDO_TIMEOUT_US] = "sdo_timeout_us",
    [K_SDO_RETRIES] = "sdo_retries",
    [K_SCHED_TICK_US] = "sched_tick_us",
    [K_HB_TIMEOUT_US] = "hb_timeout_us",
    [K_TLM_MAX_AGE_US] = "tlm_max_age_us",
    [K_RESOLUTION_REQUIRED] = "resolution_required",
    [K_WHEEL_RADIUS_M] = "wheel_radius_m",
    [K_TRACK_WIDTH_M] = "track_width_m",
    [K_COUNTS_PER_REV] = "counts_per_rev",
    [K_MAX_COUNT_DELTA] = "max_count_delta",
    [K_SPEED_RAD_S_PER_UNIT] = "speed_rad_s_per_unit",
    [K_LEFT_CHANNEL] = "left_channel",
    [K_LEFT_SIGN] = "left_sign",
    [K_RIGHT_SIGN] = "right_sign",
    [K_ODOM_MAX_AGE_US] = "odom_max_age_us",
    [K_STATUS_PERIOD_US] = "status_period_us",
    [K_AUX_PERIOD_US] = "aux_period_us",
    [K_CONTRACT_VERSION] = "contract_version",
    [K_BOOT_ID] = "boot_id",
    [K_OPERATING_STATE] = "operating_state",
    [K_BMS_REQUIRED] = "bms_required",
    [K_TFL_UNIT] = "tfl_unit",
    [K_TFL_STALE_US] = "tfl_stale_us",
    [K_MB_STALE_US] = "mb_stale_us",
    [K_MB_TIMEOUT_US] = "mb_timeout_us",
    [K_MB_GAP_US] = "mb_gap_us",
    [K_IMU_GYRO_FS] = "imu_gyro_fs",
    [K_IMU_ACCEL_FS] = "imu_accel_fs",
    [K_IMU_ODR] = "imu_odr",
    [K_IMU_STALE_US] = "imu_stale_us",
    [K_DALY_BMS_ADDR] = "daly_bms_addr",
    [K_DALY_HOST_ADDR] = "daly_host_addr",
    [K_DALY_CELLS] = "daly_cells",
    [K_DALY_TEMPS] = "daly_temps",
};

const char *oamr_sil_key_name(oamr_sil_key_t k)
{
    return (unsigned)k < (unsigned)K_COUNT ? KEY_NAMES[k] : "?";
}

bool oamr_sil_key_from_name(const char *name, oamr_sil_key_t *out)
{
    for (unsigned i = 0; i < (unsigned)K_COUNT; i++) {
        if (KEY_NAMES[i] != NULL && strcmp(KEY_NAMES[i], name) == 0) {
            *out = (oamr_sil_key_t)i;
            return true;
        }
    }
    return false;
}
