/* SPDX-License-Identifier: MIT */
/*
 * Battery management observation as seen by the core. The adapter (Daly on
 * CAN2) fills it; the safety core only reads `fault` and the data age.
 */
#ifndef OAMR_BMS_H
#define OAMR_BMS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool have_data;          /* at least one complete status set received */
    uint64_t stamp_us;       /* oldest of the required data groups */
    float pack_voltage_v;
    float pack_current_a;    /* positive = charging */
    float soc_percent;
    uint16_t cell_mv_max;
    uint16_t cell_mv_min;
    int16_t temp_c_max;
    int16_t temp_c_min;
    bool charge_mos_on;
    bool discharge_mos_on;
    bool warning;            /* any level-1 alarm */
    bool fault;              /* any level-2 alarm or hardware failure bit */
    uint8_t raw_failure[8];  /* last failure-status payload, for diagnostics */
} oamr_bms_status_t;

#ifdef __cplusplus
}
#endif

#endif /* OAMR_BMS_H */
