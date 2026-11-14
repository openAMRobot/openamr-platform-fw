/* SPDX-License-Identifier: MIT */
/*
 * CiA 301 decoders for NMT error control (heartbeat and boot-up), observed
 * NMT commands and EMCY. Decode only. This file has no NMT command encoder:
 * the base controller sending NMT to the drive is owner code (platform-fw #47).
 */
#ifndef OAMR_CO_NMT_EMCY_H
#define OAMR_CO_NMT_EMCY_H

#include <stdbool.h>
#include <stdint.h>

#include "oamr/co_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NMT states as carried in a heartbeat (CiA 301 table 59). */
typedef enum {
    OAMR_CO_NMT_BOOTUP = 0x00,
    OAMR_CO_NMT_STOPPED = 0x04,
    OAMR_CO_NMT_OPERATIONAL = 0x05,
    OAMR_CO_NMT_PRE_OPERATIONAL = 0x7F
} oamr_co_nmt_state_t;

typedef enum {
    OAMR_CO_DEC_OK = 0,
    OAMR_CO_DEC_NOT_FOR_US,   /* other COB-ID or extended frame */
    OAMR_CO_DEC_MALFORMED     /* right COB-ID, wrong length or reserved value */
} oamr_co_dec_t;

/* Heartbeat or boot-up from node_id: id 700h + node, DLC 1. Bit 7 is the
 * node-guarding toggle and must be 0 in a heartbeat; a frame with bit 7 set or
 * an unknown state value is MALFORMED. */
oamr_co_dec_t oamr_co_decode_heartbeat(const oamr_co_frame_t *f, uint8_t node_id,
                                       oamr_co_nmt_state_t *state);

/* NMT command codes (CiA 301 7.2.8.3.1), for decoding commands seen on the
 * bus from another master or a bench tool. */
typedef enum {
    OAMR_CO_NMT_CMD_START = 0x01,
    OAMR_CO_NMT_CMD_STOP = 0x02,
    OAMR_CO_NMT_CMD_ENTER_PREOP = 0x80,
    OAMR_CO_NMT_CMD_RESET_NODE = 0x81,
    OAMR_CO_NMT_CMD_RESET_COMM = 0x82
} oamr_co_nmt_cmd_t;

/* NMT command frame: id 000h, DLC 2, byte 0 command, byte 1 node (0 = all).
 * Decode only, for observation. */
oamr_co_dec_t oamr_co_decode_nmt_command(const oamr_co_frame_t *f, oamr_co_nmt_cmd_t *cmd,
                                         uint8_t *target_node);

typedef struct {
    uint16_t error_code;      /* 0000h = error reset or no error */
    uint8_t error_register;   /* copy of 1001h */
    uint8_t manufacturer[5];  /* bytes 3..7, meaning device specific */
} oamr_co_emcy_t;

/* EMCY from node_id: id 080h + node, DLC 8. */
oamr_co_dec_t oamr_co_decode_emcy(const oamr_co_frame_t *f, uint8_t node_id, oamr_co_emcy_t *out);

/* Error register 1001h bits (CiA 301 table 48). */
#define OAMR_CO_ERRREG_GENERIC 0x01u
#define OAMR_CO_ERRREG_CURRENT 0x02u
#define OAMR_CO_ERRREG_VOLTAGE 0x04u
#define OAMR_CO_ERRREG_TEMPERATURE 0x08u
#define OAMR_CO_ERRREG_COMMUNICATION 0x10u
#define OAMR_CO_ERRREG_PROFILE 0x20u
#define OAMR_CO_ERRREG_MANUFACTURER 0x80u

#ifdef __cplusplus
}
#endif

#endif /* OAMR_CO_NMT_EMCY_H */
