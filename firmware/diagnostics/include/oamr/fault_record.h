#ifndef OAMR_FAULT_RECORD_H
#define OAMR_FAULT_RECORD_H

#include "oamr/reset_cause.h"
#include "oamr/status.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OAMR_FAULT_RECORD_MAGIC UINT32_C(0x4f414652) /* bytes: R F A O */
#define OAMR_FAULT_RECORD_VERSION ((uint16_t)1u)
#define OAMR_FAULT_RECORD_SERIALIZED_SIZE ((size_t)44u)

/* CRC covers header/data bytes [0,40); stored CRC occupies bytes [40,44). */
#define OAMR_FAULT_RECORD_CRC_OFFSET ((size_t)40u)

/* Positive validation result, separate from operation success/failure. */
typedef enum {
    OAMR_FAULT_RECORD_INVALID = 0,
    OAMR_FAULT_RECORD_VALID = 1
} oamr_fault_record_validity_t;

typedef enum {
    OAMR_FAULT_VALIDATE_BAD_MAGIC = 1,
    OAMR_FAULT_VALIDATE_UNSUPPORTED_VERSION = 2,
    OAMR_FAULT_VALIDATE_BAD_LENGTH = 3,
    OAMR_FAULT_VALIDATE_CRC_MISMATCH = 4,
    OAMR_FAULT_VALIDATE_TRUNCATED = 5,
    OAMR_FAULT_VALIDATE_MALFORMED = 6
} oamr_fault_validate_error_t;

typedef struct {
    oamr_fault_record_validity_t validity;
    /* error is zero when validity is VALID; otherwise it is nonzero. */
    oamr_fault_validate_error_t error;
    size_t inspected_size;
} oamr_fault_validation_t;

/*
 * Values are diagnostics copied from reset/fault handling by the board
 * adapter. A value of zero is still a captured value; it does not establish
 * address-valid flags, exception provenance, or persistence across power loss.
 */
typedef struct {
    uint32_t raw_reset_flags;
    uint32_t stacked_pc;
    uint32_t stacked_lr;
    uint32_t stacked_xpsr;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t bfar;
    uint32_t mmar;
} oamr_fault_record_data_t;

/*
 * In-memory fixed record. Its native representation is NOT the persistence or
 * wire format: use encode/decode/validate, which define little-endian fields
 * and cover all bytes through MMFAR with CRC-32/ISO-HDLC. Reset bit
 * definitions are canonical decode context, not additional persisted fields.
 * There is no heap,
 * timestamp, validity inference, watchdog state or safety state in this type.
 */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    oamr_fault_record_data_t data;
    oamr_reset_flag_definitions_t reset_definitions;
    uint32_t crc32;
} oamr_fault_record_t;

/* IEEE CRC-32, polynomial 0xedb88320, initial/final XOR 0xffffffff. */
uint32_t oamr_fault_crc32(const void *data, size_t data_size);

/* Fully initializes magic/version/length/data/definitions and computes CRC. */
oamr_status_t oamr_fault_record_init(
    oamr_fault_record_t *record,
    const oamr_fault_record_data_t *data,
    const oamr_reset_flag_definitions_t *reset_definitions);

/* Encode exactly 44 bytes; out_size may be larger and is never truncated. */
oamr_status_t oamr_fault_record_encode(
    const oamr_fault_record_t *record,
    uint8_t *out_data,
    size_t out_capacity,
    size_t *out_size);

/*
 * Check the first length-delimited record in data. For non-truncation errors,
 * exactly 44 bytes are required. Trailing bytes belong to the caller's next
 * record/frame. CRC and all header fields are checked before VALID is returned.
 * data==NULL with size zero is treated as truncation. Output may be NULL.
 */
oamr_fault_record_validity_t oamr_fault_record_validate(
    const uint8_t *data,
    size_t data_size,
    oamr_fault_validation_t *out_validation);

/* Validate then decode one 44-byte record. out_record is zeroed on failure. */
oamr_status_t oamr_fault_record_decode(
    const uint8_t *data,
    size_t data_size,
    oamr_fault_record_t *out_record);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_FAULT_RECORD_H */
