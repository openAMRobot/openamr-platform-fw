/* SPDX-License-Identifier: MIT */
/*
 * CTL-017 lift controller, CAN3, RECEIVE-SIDE DECODE ONLY. Nothing is ever
 * transmitted by this module: no NMT, no RPDO, no SDO.
 *
 * Source: openamr-upperbody-fw PR #17 at 8dc7236e1eb38da68dabe063cd37aa11ba4c1faa,
 * docs/ctl017_object_dictionary.md (proposal v1): node-ID 0x10, EMCY 0x080 + node,
 * TPDO1 LiftStatus 0x180 + node, TPDO2 LiftStatus2 0x280 + node, heartbeat
 * 0x700 + node (decoded by oamr/co_hb_consumer.h, not here). All three PDOs
 * are 8 bytes, little-endian (sections 3, 4 and 7 of that document).
 *
 * The values are what the node says. Nothing here interprets them for motion:
 * the status_counter gap rule of D15 (gap limit 3, resync after 3 frames) is
 * the base controller's decision and is NOT implemented; gaps are only
 * counted. CTL-017 sends its PDOs in OPERATIONAL only, which needs an NMT
 * start that this image never sends.
 */
#ifndef OAMR_CTL017_RX_H
#define OAMR_CTL017_RX_H

#include <stdbool.h>
#include <stdint.h>

#include "oamr/co_frame.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define OAMR_CTL017_COB_EMCY_BASE 0x080u
#define OAMR_CTL017_COB_TPDO1_BASE 0x180u
#define OAMR_CTL017_COB_TPDO2_BASE 0x280u
#define OAMR_CTL017_POSITION_UNKNOWN 0xFFFFu
#define OAMR_CTL017_STOP_NONE 0xFFu
#define OAMR_CTL017_CMD_RESULT_NONE 0xFFu
/* TPDO1 flags (section 3) */
#define OAMR_CTL017_FLAG_POSITION_VALID 0x01u
#define OAMR_CTL017_FLAG_AT_STOP 0x02u
#define OAMR_CTL017_FLAG_MOVING 0x04u
#define OAMR_CTL017_FLAG_FAULT_PRESENT 0x20u

    typedef enum
    {
        OAMR_CTL017_RX_NOT_CONFIGURED = 0,
        OAMR_CTL017_RX_STATUS = 1,  /* TPDO1 decoded */
        OAMR_CTL017_RX_STATUS2 = 2, /* TPDO2 decoded */
        OAMR_CTL017_RX_EMCY = 3,    /* EMCY decoded */
        OAMR_CTL017_RX_OTHER =
            4, /* a frame of the node this module does not decode (heartbeat, SDO, RPDO) */
        OAMR_CTL017_RX_FOREIGN = 5,  /* another node, or not a classic 11-bit data frame */
        OAMR_CTL017_RX_MALFORMED = 6 /* right COB-ID, DLC other than 8 */
    } oamr_ctl017_rx_result_t;

    typedef struct
    {
        uint8_t node_id; /* 1..127; 0 = not configured. Source above: 0x10 */
    } oamr_ctl017_rx_config_t;

    typedef struct
    {
        /* TPDO1 */
        bool status_seen;
        uint64_t status_us; /* local receipt time of the last TPDO1 */
        uint8_t state;      /* 0 INIT .. 6 FAULT */
        uint8_t flags;
        uint16_t position_raw; /* 0.1 mm above the lower end; 0xFFFF unknown */
        uint8_t active_stop_id;
        uint8_t last_seq;
        uint8_t cmd_result;
        uint8_t status_counter;
        /* TPDO2 */
        bool status2_seen;
        uint64_t status2_us;
        uint8_t inputs;
        uint8_t target_stop_id;
        uint16_t motor_current_ma;
        int16_t velocity_raw; /* 0.1 mm/s, positive up */
        uint8_t faults_f01_f08;
        uint8_t aux_flags;
        /* EMCY */
        bool emcy_seen;
        uint64_t emcy_us;
        uint16_t emcy_code;
        uint8_t emcy_error_register;
        uint8_t emcy_fault_no;
        uint16_t emcy_fault_mask;
    } oamr_ctl017_rx_data_t;

    typedef struct
    {
        uint32_t status;
        uint32_t status2;
        uint32_t emcy;
        uint32_t other;
        uint32_t foreign;
        uint32_t malformed;
        uint32_t counter_gaps; /* TPDO1 status_counter did not advance by exactly 1 */
    } oamr_ctl017_rx_counters_t;

    typedef struct
    {
        oamr_ctl017_rx_config_t cfg;
        bool configured;
        oamr_ctl017_rx_data_t data;
        oamr_ctl017_rx_counters_t counters;
    } oamr_ctl017_rx_t;

    bool oamr_ctl017_rx_init(oamr_ctl017_rx_t *d, const oamr_ctl017_rx_config_t *cfg);
    oamr_ctl017_rx_result_t oamr_ctl017_rx_on_frame(oamr_ctl017_rx_t *d, const oamr_co_frame_t *f,
                                                    uint64_t now_us);
    /* True when a TPDO1 was seen, it flags the position valid and the position
     * is not the 0xFFFF marker. Otherwise the position is unknown. */
    bool oamr_ctl017_position_known(const oamr_ctl017_rx_t *d);
    /* Wire state name (section 3): INIT, UNHOMED, HOMING, IDLE, MOVING, STOPPING,
     * FAULT; "unknown" for any other value or before the first TPDO1. */
    const char *oamr_ctl017_state_name(const oamr_ctl017_rx_t *d);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_CTL017_RX_H */
