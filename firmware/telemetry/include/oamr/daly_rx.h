/* SPDX-License-Identifier: MIT */
/*
 * Daly BMS, CAN2, RECEIVE-SIDE DECODE ONLY. No request frames, no polling,
 * nothing is ever transmitted by this module.
 *
 * PROTOCOL NOT CONFIRMED for the delivered unit. The decoder reports every
 * decoded value with confirmed = false until the caller sets
 * protocol_confirmed = true in the configuration, and oamr_daly_rx_usable()
 * returns false until then. The owner sets that flag only after a CAN
 * capture of the delivered BMS matches the layouts below.
 *
 * Sources (see docs/architecture/sensors-and-telemetry.md):
 *   - Identifier layout: Daly CAN Communications Protocol V1.0 (2019), as
 *     summarised in the base-controller design: 29-bit identifier
 *     0x18 << 24 | DataID << 16 | destination << 8 | source.
 *   - Payload layouts of Data IDs 0x90 to 0x98: the public Daly RS485/UART
 *     protocol (V1.2), which uses the same Data IDs and 8-byte payloads.
 *   - Newer Daly H/K/M/S-series firmware is reported (public ESPHome
 *     component esphome-daly-hkms-bms-can) to use a different register-based
 *     CAN protocol. The robot uses an M-series BMS, so the V1.0 layout may
 *     not apply at all. This is the main reason for protocol_confirmed.
 *
 * Addresses have no default: the caller supplies the BMS and host addresses
 * (V1.0 documents BMS 0x01 and upper computer 0x40; TODO(Evans) confirm on
 * the delivered unit).
 *
 * Unknown Data IDs are counted, never guessed. Frames with the wrong
 * addresses, standard identifiers or a DLC other than 8 are counted.
 *
 * Current sign: the two public references disagree on whether a positive
 * current means charging or discharging. This decoder reports the signed
 * value (raw - 30000) * 0.1 A and does not name a direction.
 * TODO(Evans): settle the sign with a capture while charging.
 */
#ifndef OAMR_DALY_RX_H
#define OAMR_DALY_RX_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OAMR_DALY_PRIORITY 0x18u
#define OAMR_DALY_ID_SOC 0x90u
#define OAMR_DALY_ID_CELL_MINMAX 0x91u
#define OAMR_DALY_ID_TEMP_MINMAX 0x92u
#define OAMR_DALY_ID_MOS 0x93u
#define OAMR_DALY_ID_STATUS 0x94u
#define OAMR_DALY_ID_CELL_VOLTAGES 0x95u
#define OAMR_DALY_ID_CELL_TEMPS 0x96u
#define OAMR_DALY_ID_BALANCE 0x97u
#define OAMR_DALY_ID_FAILURE 0x98u
#define OAMR_DALY_CURRENT_OFFSET 30000
#define OAMR_DALY_TEMP_OFFSET 40
#define OAMR_DALY_MAX_CELLS 48u
#define OAMR_DALY_MAX_TEMPS 16u

/* Failure bytes 0 to 3 (V1.2): even bit = level-1 alarm, odd bit = level-2. */
#define OAMR_DALY_LEVEL1_MASK 0x55u
#define OAMR_DALY_LEVEL2_MASK 0xAAu

typedef struct {
    bool protocol_confirmed; /* false until a capture of the delivered unit matches */
    bool addresses_set;      /* must be true; there is no default address */
    uint8_t bms_addr;        /* TODO(Evans): V1.0 documents 0x01 */
    uint8_t host_addr;       /* TODO(Evans): V1.0 documents 0x40 */
    uint8_t n_cells;         /* 1..48, for range checks of 0x95. TODO(Evans): 8 for 8S */
    uint8_t n_temps;         /* 1..16, for range checks of 0x96. TODO(Evans) */
} oamr_daly_rx_config_t;

typedef enum {
    OAMR_DALY_RX_NOT_CONFIGURED = 0,
    OAMR_DALY_RX_DECODED = 1,
    OAMR_DALY_RX_FOREIGN = 2,     /* not a Daly response for this BMS and host */
    OAMR_DALY_RX_MALFORMED = 3,   /* DLC or content out of range */
    OAMR_DALY_RX_UNKNOWN_ID = 4   /* Daly identifier with an undocumented Data ID */
} oamr_daly_rx_result_t;

typedef struct {
    uint32_t id;    /* 29-bit identifier */
    bool extended;
    uint8_t dlc;
    uint8_t data[8];
} oamr_daly_rx_frame_t;

typedef struct {
    bool confirmed;            /* copy of protocol_confirmed at decode time */
    /* 0x90 */
    bool soc_seen;
    uint32_t soc_stamp_us;
    float pack_voltage_v;
    float acquired_voltage_v;  /* bytes 2..3; meaning differs between sources */
    float current_a;           /* signed, direction not named, see header */
    float soc_percent;
    /* 0x91 */
    bool cell_minmax_seen;
    uint32_t cell_minmax_stamp_us;
    uint16_t cell_max_mv;
    uint8_t cell_max_no;
    uint16_t cell_min_mv;
    uint8_t cell_min_no;
    /* 0x92 */
    bool temp_minmax_seen;
    uint32_t temp_minmax_stamp_us;
    int16_t temp_max_c;
    uint8_t temp_max_no;
    int16_t temp_min_c;
    uint8_t temp_min_no;
    /* 0x93 */
    bool mos_seen;
    uint32_t mos_stamp_us;
    uint8_t charge_state;      /* raw: V1.2 says 0 stationary, 1 charge, 2 discharge */
    bool charge_mos_on;
    bool discharge_mos_on;
    uint8_t bms_life;
    uint32_t remaining_mah;
    /* 0x94 */
    bool status_seen;
    uint32_t status_stamp_us;
    uint8_t n_cells_reported;
    uint8_t n_temps_reported;
    bool charger_present;
    bool load_present;
    uint8_t dio_bits;
    uint16_t cycles;
    /* 0x95 */
    uint16_t cell_mv[OAMR_DALY_MAX_CELLS];
    uint64_t cell_seen_mask;
    /* 0x96 */
    int16_t cell_temp_c[OAMR_DALY_MAX_TEMPS];
    uint32_t temp_seen_mask;
    /* 0x97 */
    bool balance_seen;
    uint8_t balance_raw[6];
    /* 0x98 */
    bool failure_seen;
    uint32_t failure_stamp_us;
    uint8_t failure_raw[8];
    bool any_level1;           /* any level-1 alarm in bytes 0..3 */
    bool any_level2;           /* any level-2 alarm in bytes 0..3 */
    bool any_hw_failure;       /* any bit in bytes 4..6 */
} oamr_daly_rx_data_t;

typedef struct {
    uint32_t decoded;
    uint32_t foreign;
    uint32_t malformed;
    uint32_t unknown_id;
    uint32_t unknown_id_last; /* last undocumented Data ID seen, for diagnostics */
} oamr_daly_rx_counters_t;

typedef struct {
    oamr_daly_rx_config_t cfg;
    bool configured;
    oamr_daly_rx_data_t data;
    oamr_daly_rx_counters_t counters;
} oamr_daly_rx_t;

bool oamr_daly_rx_init(oamr_daly_rx_t *d, const oamr_daly_rx_config_t *cfg);
/* Feed every frame received on CAN2 with its local receipt time. */
oamr_daly_rx_result_t oamr_daly_rx_on_frame(oamr_daly_rx_t *d, const oamr_daly_rx_frame_t *f,
                                            uint32_t now_us);
/* Identifier a BMS response with this Data ID would carry (for filters). */
uint32_t oamr_daly_rx_expected_id(const oamr_daly_rx_t *d, uint8_t data_id);
/* True only when configured AND protocol_confirmed. A consumer must not use
 * decoded values for any decision while this is false. */
bool oamr_daly_rx_usable(const oamr_daly_rx_t *d);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_DALY_RX_H */
