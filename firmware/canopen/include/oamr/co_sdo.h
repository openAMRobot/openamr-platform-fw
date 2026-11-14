/* SPDX-License-Identifier: MIT */
/*
 * CiA 301 SDO client codec: expedited transfers only. Pure functions, no
 * state, no transmit. A caller builds a frame with an encode function and
 * hands it to its own transmit path; a caller hands a received frame to a
 * decode function and gets a classified result.
 *
 * Reused from platform-fw #46 (motor_controller_bridge canopen_sdo.c): the
 * command specifier values and the n/e/s size rule for expedited uploads.
 * Changed: split into stateless encode/decode so read-only users cannot
 * reach a write path; strict DLC 8 and standard-id checks; download and
 * segment responses to an upload are reported as protocol errors rather
 * than as aborts; abort frames carry their own index and subindex.
 *
 * oamr_co_sdo_encode_download_expedited() exists as a tested codec function
 * only. Nothing in firmware/canopen or firmware/drive_telemetry calls it.
 */
#ifndef OAMR_CO_SDO_H
#define OAMR_CO_SDO_H

#include <stdbool.h>
#include <stdint.h>

#include "oamr/co_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Command specifier bytes (byte 0). */
#define OAMR_CO_SDO_CCS_UPLOAD_INIT 0x40u     /* client: initiate upload request */
#define OAMR_CO_SDO_SCS_UPLOAD_INIT 0x40u     /* server: bits 7..5 = 010 */
#define OAMR_CO_SDO_SCS_DOWNLOAD_INIT 0x60u   /* server: bits 7..5 = 011 */
#define OAMR_CO_SDO_CS_ABORT 0x80u

/* Abort codes from CiA 301 table 22 (subset used for naming). */
#define OAMR_CO_SDO_ABORT_TOGGLE 0x05030000u
#define OAMR_CO_SDO_ABORT_TIMEOUT 0x05040000u
#define OAMR_CO_SDO_ABORT_BAD_CS 0x05040001u
#define OAMR_CO_SDO_ABORT_UNSUPPORTED_ACCESS 0x06010000u
#define OAMR_CO_SDO_ABORT_WRITE_ONLY 0x06010001u
#define OAMR_CO_SDO_ABORT_READ_ONLY 0x06010002u
#define OAMR_CO_SDO_ABORT_NO_OBJECT 0x06020000u
#define OAMR_CO_SDO_ABORT_LENGTH 0x06070010u
#define OAMR_CO_SDO_ABORT_NO_SUBINDEX 0x06090011u
#define OAMR_CO_SDO_ABORT_VALUE_RANGE 0x06090030u
#define OAMR_CO_SDO_ABORT_GENERAL 0x08000000u
#define OAMR_CO_SDO_ABORT_DATA_TRANSFER 0x08000020u
#define OAMR_CO_SDO_ABORT_DATA_LOCAL 0x08000021u
#define OAMR_CO_SDO_ABORT_DEVICE_STATE 0x08000022u

typedef enum {
    OAMR_CO_SDO_RX_OK = 0,           /* expedited upload response for the expected object */
    OAMR_CO_SDO_RX_ABORT,            /* abort for the expected object; abort_code set */
    OAMR_CO_SDO_RX_NOT_FOR_US,       /* not an SDO response from the expected node */
    OAMR_CO_SDO_RX_MALFORMED,        /* right COB-ID but DLC != 8 or bad specifier */
    OAMR_CO_SDO_RX_WRONG_OBJECT,     /* well-formed but index or subindex echo differs */
    OAMR_CO_SDO_RX_SEGMENTED,        /* server started a segmented upload: not supported */
    OAMR_CO_SDO_RX_UNEXPECTED_CS     /* download ack or segment response to an upload */
} oamr_co_sdo_rx_t;

typedef struct {
    uint16_t index;          /* as echoed by the server */
    uint8_t subindex;
    uint8_t size;            /* 1..4 when size_indicated, else 4 */
    bool size_indicated;     /* s bit: the server stated the data size */
    uint32_t value;          /* little-endian payload, masked to size */
    uint32_t abort_code;     /* valid for OAMR_CO_SDO_RX_ABORT */
} oamr_co_sdo_response_t;

/* Initiate expedited upload (read) request to node_id. Returns false for an
 * invalid node id or a null frame. */
bool oamr_co_sdo_encode_upload_request(uint8_t node_id, uint16_t index, uint8_t subindex,
                                       oamr_co_frame_t *out);

/* Decode a frame as the response to an upload of index:subindex from node_id.
 * out is always cleared first; on anything but OK and ABORT its fields are
 * not a sample and must not be used as one. */
oamr_co_sdo_rx_t oamr_co_sdo_decode_upload_response(const oamr_co_frame_t *f, uint8_t node_id,
                                                    uint16_t index, uint8_t subindex,
                                                    oamr_co_sdo_response_t *out);

/* Decode any SDO abort frame (either direction) for node_id. Returns false if
 * the frame is not a well-formed abort; else fills index, subindex, code. */
bool oamr_co_sdo_decode_abort(const oamr_co_frame_t *f, uint32_t cob_id, uint16_t *index,
                              uint8_t *subindex, uint32_t *abort_code);

/* Short English name of an abort code for logs; "unknown" for others. */
const char *oamr_co_sdo_abort_name(uint32_t abort_code);

/* Codec only. Initiate expedited download (write) request, size 1, 2 or 4.
 * Not called by any production code in this package; see file header. */
bool oamr_co_sdo_encode_download_expedited(uint8_t node_id, uint16_t index, uint8_t subindex,
                                           uint32_t value, uint8_t size, oamr_co_frame_t *out);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_CO_SDO_H */
