/* SPDX-License-Identifier: MIT */
#ifndef OAMR_FAULT_RETAINED_H
#define OAMR_FAULT_RETAINED_H

#include "oamr/exception_frame.h"
#include "oamr/fault_record.h"


#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Two fixed slots in caller-owned, statically allocated retained storage.
 * This module provides reset/interrupted-write consistency only. It does not
 * choose a memory technology, initialize a backup domain, or guarantee
 * retention across power loss. The board integration must establish those
 * properties separately.
 */
#define OAMR_FAULT_RETAINED_SLOT_COUNT ((size_t)2u)
#define OAMR_FAULT_RETAINED_STATE_OFFSET ((size_t)0u)
#define OAMR_FAULT_RETAINED_METADATA_OFFSET ((size_t)4u)
#define OAMR_FAULT_RETAINED_METADATA_SIZE ((size_t)24u)
#define OAMR_FAULT_RETAINED_PAYLOAD_OFFSET ((size_t)28u)
#define OAMR_FAULT_RETAINED_PAYLOAD_CRC_OFFSET ((size_t)72u)
#define OAMR_FAULT_RETAINED_COMMIT_CRC_OFFSET ((size_t)76u)
#define OAMR_FAULT_RETAINED_SLOT_BYTES ((size_t)80u)
#define OAMR_FAULT_RETAINED_STORAGE_BYTES ((size_t)160u)
#define OAMR_FAULT_RETAINED_FORMAT_VERSION ((uint16_t)1u)

/* Raw atomic state values. Only this module changes them through its API. */
typedef enum {
    OAMR_FAULT_RETAINED_STATE_EMPTY = 0,
    OAMR_FAULT_RETAINED_STATE_WRITING = 0x57525447,
    OAMR_FAULT_RETAINED_STATE_COMMITTED = 0x434d5444,
    OAMR_FAULT_RETAINED_STATE_RETIRED = 0x52545244
} oamr_fault_retained_state_t;

/* max_align_t keeps every slot's atomic state suitably aligned. */
typedef union {
    max_align_t alignment;
    uint8_t bytes[OAMR_FAULT_RETAINED_STORAGE_BYTES];
} oamr_fault_retained_storage_t;

typedef struct {
    oamr_fault_record_t record;
    oamr_exception_capture_metadata_t metadata;
    size_t slot_index;
} oamr_fault_retained_snapshot_t;

typedef struct {
    oamr_fault_retained_storage_t *storage;
    /* Internal next sequence; callers must not modify it. */
    uint32_t next_sequence;
} oamr_fault_retained_t;

_Static_assert(sizeof(oamr_exception_capture_metadata_t) == 12u,
               "exception metadata must remain the frozen 12-byte interface");
_Static_assert(OAMR_FAULT_RETAINED_METADATA_OFFSET +
               OAMR_FAULT_RETAINED_METADATA_SIZE ==
               OAMR_FAULT_RETAINED_PAYLOAD_OFFSET,
               "retained metadata/payload layout mismatch");
_Static_assert(OAMR_FAULT_RETAINED_PAYLOAD_OFFSET +
               OAMR_FAULT_RECORD_SERIALIZED_SIZE ==
               OAMR_FAULT_RETAINED_PAYLOAD_CRC_OFFSET,
               "retained payload layout mismatch");
_Static_assert(OAMR_FAULT_RETAINED_COMMIT_CRC_OFFSET + sizeof(uint32_t) ==
               OAMR_FAULT_RETAINED_SLOT_BYTES,
               "retained slot size mismatch");
_Static_assert(OAMR_FAULT_RETAINED_SLOT_COUNT *
               OAMR_FAULT_RETAINED_SLOT_BYTES ==
               OAMR_FAULT_RETAINED_STORAGE_BYTES,
               "retained storage must contain exactly two slots");
_Static_assert(sizeof(oamr_fault_retained_storage_t) >=
               OAMR_FAULT_RETAINED_STORAGE_BYTES,
               "retained storage type is too small");
_Static_assert(sizeof(uint32_t) == sizeof(uint32_t),
               "retained state must be an atomic 32-bit word");
_Static_assert(_Alignof(oamr_fault_retained_storage_t) >=
               _Alignof(uint32_t),
               "retained storage alignment is insufficient");

/*
 * Bind existing bytes without clearing or otherwise writing storage. Valid
 * committed and retired records determine the next sequence. Invalid,
 * incomplete and blank records are ignored for sequence recovery.
 * Returns INVALID only for null arguments.
 */
oamr_status_t oamr_fault_retained_init(
    oamr_fault_retained_t *retained,
    oamr_fault_retained_storage_t *storage);

/*
 * Commit one validated schema-v1 payload and generic exception metadata. The
 * input metadata.sequence is ignored: retention assigns sequence internally.
 * Empty/invalid/retired slots are used before the older of two valid slots.
 * COMMITTED is published last with compiler-ordered aligned stores. A new interrupted write
 * therefore leaves either the previous valid record or no record, never an
 * intentionally mixed valid record.
 *
 * out_sequence is optional and is set to zero before validation. On success it
 * receives the assigned sequence. Null retained/record/metadata, a null bound
 * storage pointer, or an invalid record returns INVALID without a slot write.
 * STALE means unstable storage prevented safe writer selection.
 *
 * The caller serializes init/commit/retire writers. No allocation, lock, HAL,
 * watchdog, UART, actuation, or safety-state operation is performed.
 */
oamr_status_t oamr_fault_retained_commit(
    oamr_fault_retained_t *retained,
    const oamr_fault_record_t *record,
    const oamr_exception_capture_metadata_t *metadata,
    uint32_t *out_sequence);

/*
 * Load the newest fully committed snapshot using unsigned serial comparison.
 * Equal sequences select slot zero deterministically. The returned fault
 * record has zero reset definitions; the board supplies its reviewed masks.
 *
 * OK returns a snapshot. EMPTY means no valid committed record exists,
 * including blank, retired, or interrupted-WRITING slots. INVALID means null
 * arguments/storage or malformed/random/corrupt storage with no valid record.
 * STALE means a state changed during a snapshot and no stable valid record was
 * found. Every failure zeroes a non-null output snapshot.
 */
oamr_status_t oamr_fault_retained_load(
    const oamr_fault_retained_t *retained,
    oamr_fault_retained_snapshot_t *out_snapshot);

/*
 * Retire only the exact validated snapshot in the identified slot. Sequence,
 * payload and metadata must match. A stale or different/newer record is never
 * retired. The marker becomes RETIRED after payload/state ordering barriers;
 * payload bytes remain available for sequence recovery until slot reuse.
 *
 * OK marks that exact record retired. EMPTY means no matching valid record.
 * INVALID means null arguments/storage or an invalid slot index.
 * STALE means a state changed during validation or retirement.
 */
oamr_status_t oamr_fault_retained_retire(
    oamr_fault_retained_t *retained,
    size_t slot_index,
    const oamr_fault_retained_snapshot_t *snapshot);

/* Internal stage identifiers are always defined so production builds can
 * compile the observer away. Only the external test observer is conditional. */
typedef enum {
    OAMR_FAULT_RETAINED_TEST_MARK_WRITING = 1,
    OAMR_FAULT_RETAINED_TEST_METADATA_WRITTEN = 2,
    OAMR_FAULT_RETAINED_TEST_PAYLOAD_WRITTEN = 3,
    OAMR_FAULT_RETAINED_TEST_PAYLOAD_CRC_WRITTEN = 4,
    OAMR_FAULT_RETAINED_TEST_COMMIT_CRC_WRITTEN = 5,
    OAMR_FAULT_RETAINED_TEST_COMMITTED = 6,
    OAMR_FAULT_RETAINED_TEST_RETIRED = 7,
    OAMR_FAULT_RETAINED_TEST_AFTER_STATE_READ = 8,
    OAMR_FAULT_RETAINED_TEST_BYTE_WRITTEN = 9
} oamr_fault_retained_test_stage_t;

#if defined(OAMR_FAULT_RETAINED_TEST_HOOK)
/* Test-only, read-only observer seam. Production builds contain no observer
 * call or external symbol. Tests compile this module with the macro defined
 * and supply this function to capture each completed write stage, or to model
 * a state change after a reader's first acquire. The observer receives no
 * mutable storage pointer and no arbitrary-state mutation API is exposed. */
void oamr_fault_retained_test_observe(
    const oamr_fault_retained_storage_t *storage,
    size_t slot_index,
    oamr_fault_retained_test_stage_t stage);
#endif

#ifdef __cplusplus
}
#endif

#endif
