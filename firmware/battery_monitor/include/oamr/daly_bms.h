/* SPDX-License-Identifier: MIT */
/*
 * Daly BMS on CAN2 (250 kbit/s, proprietary request/response, not CANopen).
 *
 * UNCONFIRMED for the delivered Daly M-series order code. Sources: Daly CAN
 * Communications Protocol V1.0 (2019) as summarised in Evans's I8 design
 * (29-bit IDs 0x18 | DataID | destination | source, host 0x40, BMS 0x01,
 * Data IDs 0x90 to 0x98) and the payload layouts of the same Data IDs in the
 * public Daly UART/485 Communications Protocol V1.2. Newer Daly H/K/M/S
 * firmware may use a different register protocol on CAN; check the delivered
 * unit before trusting any value. TODO(Evans).
 *
 * Payload layouts used (8 data bytes, big endian):
 *   0x90 pack voltage 0.1 V [0..1], current (raw - 30000) * 0.1 A [4..5], SOC 0.1 % [6..7]
 *   0x91 max cell mV [0..1], cell no [2], min cell mV [3..4], cell no [5]
 *   0x92 max temp degC + 40 [0], sensor no [1], min temp degC + 40 [2], sensor no [3]
 *   0x93 state [0], charge MOS [1], discharge MOS [2], life [3], remaining mAh [4..7]
 *   0x98 failure status bit field [0..7]
 * Fault policy (proposal, TODO(Evans)): any level-2 alarm in bytes 0..3 or any
 * bit in bytes 4..6 is a fault; any level-1 alarm is a warning.
 */
#ifndef OAMR_DALY_BMS_H
#define OAMR_DALY_BMS_H

#include <stdbool.h>
#include <stdint.h>

#include "oamr/bms.h"
#include "oamr/can.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OAMR_DALY_HOST_ADDR 0x40u
#define OAMR_DALY_BMS_ADDR 0x01u
#define OAMR_DALY_ID_PACK 0x90u
#define OAMR_DALY_ID_CELL_MINMAX 0x91u
#define OAMR_DALY_ID_TEMP_MINMAX 0x92u
#define OAMR_DALY_ID_MOS 0x93u
#define OAMR_DALY_ID_STATUS 0x94u
#define OAMR_DALY_ID_CELL_VOLTAGES 0x95u
#define OAMR_DALY_ID_CELL_TEMPS 0x96u
#define OAMR_DALY_ID_BALANCE 0x97u
#define OAMR_DALY_ID_FAILURE 0x98u

/* Level-2 masks for bytes 0..3 (odd bits are level 2 in the V1.2 layout). */
#define OAMR_DALY_LEVEL2_MASK 0xAAu
#define OAMR_DALY_LEVEL1_MASK 0x55u

typedef struct {
    uint64_t request_period_us;  /* time between requests (one Data ID each) */
    uint64_t stale_us;           /* TODO(Evans): 3 s (P-02 rev19.2); I8 WP lists 10 s for park */
} oamr_daly_config_t;

typedef struct {
    oamr_daly_config_t cfg;
    oamr_can_tx_t tx;
    uint8_t next;                /* index into the request list */
    uint64_t last_request_us;
    bool requested_once;
    uint64_t rx_us[5];           /* receive time per required Data ID */
    bool rx_seen[5];
    oamr_bms_status_t status;
    uint32_t frames_rx;
    uint32_t frames_ignored;
    uint32_t tx_failures;
} oamr_daly_t;

void oamr_daly_default_config(oamr_daly_config_t *cfg);
void oamr_daly_init(oamr_daly_t *d, const oamr_daly_config_t *cfg, const oamr_can_tx_t *tx);
uint32_t oamr_daly_request_id(uint8_t data_id);
uint32_t oamr_daly_response_id(uint8_t data_id);
/* Call from the background loop; sends at most one request per period. */
void oamr_daly_poll(oamr_daly_t *d, uint64_t now_us);
/* Feed every frame received on CAN2. Returns true if it was a Daly response. */
bool oamr_daly_on_frame(oamr_daly_t *d, const oamr_can_frame_t *f, uint64_t now_us);
/* Fresh: every required Data ID received within stale_us. */
bool oamr_daly_fresh(const oamr_daly_t *d, uint64_t now_us);
const oamr_bms_status_t *oamr_daly_status(const oamr_daly_t *d);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_DALY_BMS_H */
