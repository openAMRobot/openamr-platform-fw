/* SPDX-License-Identifier: MIT */
#include "oamr/fault_retained.h"

#include <stdbool.h>
#include <string.h>

#if defined(__arm__) || defined(__thumb__)
#define OAMR_RETAINED_DMB() __asm volatile ("dmb 0xF" : : : "memory")
#else
#define OAMR_RETAINED_DMB() __asm volatile ("" : : : "memory")
#endif

/* A Cortex-M7 aligned 32-bit load/store is single-copy atomic. Explicit
 * volatile accesses and compiler barriers keep publication ordered without
 * assuming that C11 atomic typedefs have identical representations on every
 * host used for fault-injection tests. The caller serializes writer/retire. */
_Static_assert(OAMR_FAULT_RETAINED_SLOT_BYTES % _Alignof(uint32_t) == 0u,
               "every slot must preserve state alignment");

static uint32_t oamr_retained_get_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void oamr_retained_put_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value & UINT32_C(0xff));
    data[1] = (uint8_t)((value >> 8) & UINT32_C(0xff));
    data[2] = (uint8_t)((value >> 16) & UINT32_C(0xff));
    data[3] = (uint8_t)((value >> 24) & UINT32_C(0xff));
}

static void oamr_retained_put_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value & UINT16_C(0xff));
    data[1] = (uint8_t)((value >> 8) & UINT16_C(0xff));
}

static void oamr_retained_observe(
    const oamr_fault_retained_t *retained,
    size_t slot_index,
    int stage)
{
#if defined(OAMR_FAULT_RETAINED_TEST_HOOK)
    oamr_fault_retained_test_observe(
        retained->storage, slot_index,
        (oamr_fault_retained_test_stage_t)stage);
#else
    (void)retained;
    (void)slot_index;
    (void)stage;
#endif
}

static uint8_t *oamr_retained_slot_bytes(
    const oamr_fault_retained_t *retained,
    size_t slot_index)
{
    return &retained->storage->bytes[
        slot_index * OAMR_FAULT_RETAINED_SLOT_BYTES];
}

static volatile uint32_t *oamr_retained_state_cell(
    const oamr_fault_retained_t *retained,
    size_t slot_index)
{
    return (volatile uint32_t *)(void *)oamr_retained_slot_bytes(
        retained, slot_index);
}

static uint32_t oamr_retained_read_state(
    const oamr_fault_retained_t *retained,
    size_t slot_index)
{
    uint32_t state;
    __asm volatile ("" : : : "memory");
    state = *oamr_retained_state_cell(retained, slot_index);
    OAMR_RETAINED_DMB();
    __asm volatile ("" : : : "memory");
    oamr_retained_observe(retained, slot_index,
        OAMR_FAULT_RETAINED_TEST_AFTER_STATE_READ);
    return state;
}

static void oamr_retained_write_bytes(
    oamr_fault_retained_t *retained,
    size_t slot_index,
    size_t offset,
    const uint8_t *data,
    size_t size)
{
    size_t index;
    uint8_t *slot = oamr_retained_slot_bytes(retained, slot_index);

    for (index = 0u; index < size; ++index) {
        slot[offset + index] = data[index];
        /* Test images can stop after each individual persisted byte. */
        oamr_retained_observe(retained, slot_index,
            OAMR_FAULT_RETAINED_TEST_BYTE_WRITTEN);
    }
}

static bool oamr_retained_known_state(uint32_t state)
{
    return state == (uint32_t)OAMR_FAULT_RETAINED_STATE_EMPTY ||
           state == (uint32_t)OAMR_FAULT_RETAINED_STATE_WRITING ||
           state == (uint32_t)OAMR_FAULT_RETAINED_STATE_COMMITTED ||
           state == (uint32_t)OAMR_FAULT_RETAINED_STATE_RETIRED;
}

/* Unsigned serial comparison: all 32-bit values, including zero/wrap, order. */
static int oamr_retained_sequence_compare(uint32_t left, uint32_t right)
{
    uint32_t difference = left - right;
    if (difference == 0u) {
        return 0;
    }
    return difference < UINT32_C(0x80000000) ? 1 : -1;
}

static oamr_status_t oamr_retained_read_metadata(
    const uint8_t *slot,
    oamr_exception_capture_metadata_t *metadata,
    uint32_t *out_stored_crc)
{
    uint8_t metadata_bytes[OAMR_FAULT_RETAINED_METADATA_SIZE];
    uint32_t computed_crc;
    uint32_t stored_commit_crc;

    (void)memcpy(metadata_bytes,
                 slot + OAMR_FAULT_RETAINED_METADATA_OFFSET,
                 sizeof(metadata_bytes));
    if (oamr_retained_get_u32(metadata_bytes + 0u) !=
            OAMR_FAULT_RETAINED_FORMAT_VERSION ||
        oamr_retained_get_u32(metadata_bytes + 4u) !=
            OAMR_FAULT_RETAINED_METADATA_SIZE) {
        return OAMR_STATUS_INVALID;
    }
    metadata->sequence = oamr_retained_get_u32(metadata_bytes + 8u);
    metadata->exc_return = oamr_retained_get_u32(metadata_bytes + 12u);
    metadata->exception_kind = (uint16_t)(
        (uint16_t)metadata_bytes[16u] |
        ((uint16_t)metadata_bytes[17u] << 8));
    metadata->flags = (uint16_t)(
        (uint16_t)metadata_bytes[18u] |
        ((uint16_t)metadata_bytes[19u] << 8));
    *out_stored_crc = oamr_retained_get_u32(
        slot + OAMR_FAULT_RETAINED_PAYLOAD_CRC_OFFSET);
    stored_commit_crc = oamr_retained_get_u32(
        slot + OAMR_FAULT_RETAINED_COMMIT_CRC_OFFSET);
    computed_crc = oamr_fault_crc32(metadata_bytes, sizeof(metadata_bytes));
    computed_crc = oamr_fault_crc32(
        slot + OAMR_FAULT_RETAINED_PAYLOAD_OFFSET,
        OAMR_FAULT_RECORD_SERIALIZED_SIZE) ^ computed_crc;
    if (computed_crc != stored_commit_crc) {
        return OAMR_STATUS_INVALID;
    }
    return OAMR_STATUS_OK;
}

/* State is read before and after every byte/metadata/payload snapshot. */
static oamr_status_t oamr_retained_validate_slot(
    const oamr_fault_retained_t *retained,
    size_t slot_index,
    uint32_t required_state,
    oamr_fault_retained_snapshot_t *out_snapshot,
    bool *out_found)
{
    uint8_t *slot = oamr_retained_slot_bytes(retained, slot_index);
    uint32_t state = oamr_retained_read_state(
        retained, slot_index);
    uint32_t end_state;
    uint32_t payload_crc;
    uint32_t stored_payload_crc;
    oamr_fault_validation_t validation;
    oamr_fault_retained_snapshot_t snapshot;
    oamr_status_t status;

    *out_found = false;
    (void)memset(&snapshot, 0, sizeof(snapshot));
    if (state != required_state) {
        return oamr_retained_known_state(state) ?
            OAMR_STATUS_EMPTY : OAMR_STATUS_INVALID;
    }

    status = oamr_retained_read_metadata(
        slot, &snapshot.metadata, &stored_payload_crc);
    if (status != OAMR_STATUS_OK) {
        return OAMR_STATUS_INVALID;
    }
    if (oamr_fault_record_validate(
            slot + OAMR_FAULT_RETAINED_PAYLOAD_OFFSET,
            OAMR_FAULT_RECORD_SERIALIZED_SIZE, &validation) !=
        OAMR_FAULT_RECORD_VALID) {
        return OAMR_STATUS_INVALID;
    }
    payload_crc = oamr_fault_crc32(
        slot + OAMR_FAULT_RETAINED_PAYLOAD_OFFSET,
        OAMR_FAULT_RECORD_SERIALIZED_SIZE);
    if (payload_crc != stored_payload_crc) {
        return OAMR_STATUS_INVALID;
    }
    status = oamr_fault_record_decode(
        slot + OAMR_FAULT_RETAINED_PAYLOAD_OFFSET,
        OAMR_FAULT_RECORD_SERIALIZED_SIZE, &snapshot.record);
    if (status != OAMR_STATUS_OK) {
        return OAMR_STATUS_INVALID;
    }

    end_state = oamr_retained_read_state(
        retained, slot_index);
    if (end_state != state) {
        return OAMR_STATUS_STALE;
    }
    if (out_snapshot != NULL) {
        snapshot.slot_index = slot_index;
        *out_snapshot = snapshot;
    }
    *out_found = true;
    return OAMR_STATUS_OK;
}

static oamr_status_t oamr_retained_load_any(
    const oamr_fault_retained_t *retained,
    oamr_fault_retained_snapshot_t *out_snapshot)
{
    oamr_fault_retained_snapshot_t candidates[
        OAMR_FAULT_RETAINED_SLOT_COUNT];
    bool found[OAMR_FAULT_RETAINED_SLOT_COUNT] = {false, false};
    oamr_status_t status = OAMR_STATUS_EMPTY;
    size_t selected = OAMR_FAULT_RETAINED_SLOT_COUNT;
    size_t index;

    for (index = 0u; index < OAMR_FAULT_RETAINED_SLOT_COUNT; ++index) {
        oamr_status_t candidate_status = oamr_retained_validate_slot(
            retained, index,
            (uint32_t)OAMR_FAULT_RETAINED_STATE_COMMITTED,
            &candidates[index], &found[index]);
        if (candidate_status == OAMR_STATUS_STALE) {
            status = OAMR_STATUS_STALE;
        } else if (candidate_status == OAMR_STATUS_INVALID &&
                   status == OAMR_STATUS_EMPTY) {
            status = OAMR_STATUS_INVALID;
        }
    }

    for (index = 0u; index < OAMR_FAULT_RETAINED_SLOT_COUNT; ++index) {
        if (!found[index]) {
            continue;
        }
        if (selected == OAMR_FAULT_RETAINED_SLOT_COUNT ||
            oamr_retained_sequence_compare(
                candidates[index].metadata.sequence,
                candidates[selected].metadata.sequence) > 0) {
            selected = index;
        }
    }

    if (selected == OAMR_FAULT_RETAINED_SLOT_COUNT) {
        return status;
    }
    if (out_snapshot != NULL) {
        *out_snapshot = candidates[selected];
    }
    return OAMR_STATUS_OK;
}

static void oamr_retained_scan_sequence(
    const oamr_fault_retained_t *retained,
    uint32_t *out_next_sequence)
{
    oamr_fault_retained_snapshot_t snapshot;
    uint32_t newest = 0u;
    bool have_sequence = false;
    size_t index;

    for (index = 0u; index < OAMR_FAULT_RETAINED_SLOT_COUNT; ++index) {
        bool found = false;
        oamr_status_t status = oamr_retained_validate_slot(
            retained, index,
            (uint32_t)OAMR_FAULT_RETAINED_STATE_COMMITTED,
            &snapshot, &found);
        if (status == OAMR_STATUS_OK && found) {
            if (!have_sequence || oamr_retained_sequence_compare(
                    snapshot.metadata.sequence, newest) > 0) {
                newest = snapshot.metadata.sequence;
                have_sequence = true;
            }
        }
        status = oamr_retained_validate_slot(
            retained, index,
            (uint32_t)OAMR_FAULT_RETAINED_STATE_RETIRED,
            &snapshot, &found);
        if (status == OAMR_STATUS_OK && found) {
            if (!have_sequence || oamr_retained_sequence_compare(
                    snapshot.metadata.sequence, newest) > 0) {
                newest = snapshot.metadata.sequence;
                have_sequence = true;
            }
        }
    }
    *out_next_sequence = have_sequence ? newest + 1u : 1u;
}

static void oamr_retained_write_metadata(
    oamr_fault_retained_t *retained,
    size_t slot_index,
    uint32_t sequence,
    const oamr_exception_capture_metadata_t *metadata)
{
    uint8_t encoded[OAMR_FAULT_RETAINED_METADATA_SIZE] = {0};

    oamr_retained_put_u32(encoded + 0u,
        OAMR_FAULT_RETAINED_FORMAT_VERSION);
    oamr_retained_put_u32(encoded + 4u,
        (uint32_t)OAMR_FAULT_RETAINED_METADATA_SIZE);
    oamr_retained_put_u32(encoded + 8u, sequence);
    oamr_retained_put_u32(encoded + 12u, metadata->exc_return);
    oamr_retained_put_u16(encoded + 16u, metadata->exception_kind);
    oamr_retained_put_u16(encoded + 18u, metadata->flags);
    oamr_retained_write_bytes(retained, slot_index,
        OAMR_FAULT_RETAINED_METADATA_OFFSET, encoded, sizeof(encoded));
}

static uint32_t oamr_retained_commit_crc(
    const oamr_fault_retained_t *retained,
    size_t slot_index)
{
    const uint8_t *slot = oamr_retained_slot_bytes(retained, slot_index);
    uint32_t crc = oamr_fault_crc32(
        slot + OAMR_FAULT_RETAINED_METADATA_OFFSET,
        OAMR_FAULT_RETAINED_METADATA_SIZE);
    return crc ^ oamr_fault_crc32(
        slot + OAMR_FAULT_RETAINED_PAYLOAD_OFFSET,
        OAMR_FAULT_RECORD_SERIALIZED_SIZE);
}

oamr_status_t oamr_fault_retained_init(
    oamr_fault_retained_t *retained,
    oamr_fault_retained_storage_t *storage)
{
    if (retained == NULL || storage == NULL) {
        return OAMR_STATUS_INVALID;
    }
    retained->storage = storage;
    oamr_retained_scan_sequence(retained, &retained->next_sequence);
    return OAMR_STATUS_OK;
}

oamr_status_t oamr_fault_retained_commit(
    oamr_fault_retained_t *retained,
    const oamr_fault_record_t *record,
    const oamr_exception_capture_metadata_t *metadata,
    uint32_t *out_sequence)
{
    uint8_t encoded[OAMR_FAULT_RECORD_SERIALIZED_SIZE];
    size_t encoded_size = 0u;
    uint32_t crc;
    uint32_t sequence;
    size_t target = 0u;
    bool target_found = false;
    size_t index;

    if (out_sequence != NULL) {
        *out_sequence = 0u;
    }
    if (retained == NULL || retained->storage == NULL ||
        record == NULL || metadata == NULL) {
        return OAMR_STATUS_INVALID;
    }
    if (record->magic != OAMR_FAULT_RECORD_MAGIC ||
        record->version != OAMR_FAULT_RECORD_VERSION ||
        record->length != OAMR_FAULT_RECORD_SERIALIZED_SIZE ||
        oamr_fault_record_encode(record, encoded, sizeof(encoded),
                                 &encoded_size) != OAMR_STATUS_OK ||
        oamr_fault_record_validate(encoded, encoded_size, NULL) !=
        OAMR_FAULT_RECORD_VALID) {
        return OAMR_STATUS_INVALID;
    }

    /* Prefer a non-valid slot, never overwriting the sole last valid record. */
    for (index = 0u; index < OAMR_FAULT_RETAINED_SLOT_COUNT; ++index) {
        oamr_fault_retained_snapshot_t ignored;
        bool found = false;
        oamr_status_t status = oamr_retained_validate_slot(
            retained, index,
            (uint32_t)OAMR_FAULT_RETAINED_STATE_COMMITTED,
            &ignored, &found);
        if (status == OAMR_STATUS_STALE) {
            return OAMR_STATUS_STALE;
        }
        if (status == OAMR_STATUS_OK && found) {
            continue;
        }
        /* A stable corrupt/empty/retired slot may be reused. Only a state
         * change during inspection prevents safe selection. */
        if (!found) {
            target = index;
            target_found = true;
            break;
        }
    }
    if (!target_found) {
        target = 0u;
        target_found = true;
        for (index = 1u; index < OAMR_FAULT_RETAINED_SLOT_COUNT; ++index) {
            oamr_fault_retained_snapshot_t older;
            oamr_fault_retained_snapshot_t newer;
            bool old_found = false;
            bool new_found = false;
            if (oamr_retained_validate_slot(
                    retained, target,
                    (uint32_t)OAMR_FAULT_RETAINED_STATE_COMMITTED,
                    &older, &old_found) != OAMR_STATUS_OK ||
                oamr_retained_validate_slot(
                    retained, index,
                    (uint32_t)OAMR_FAULT_RETAINED_STATE_COMMITTED,
                    &newer, &new_found) != OAMR_STATUS_OK ||
                !old_found || !new_found) {
                return OAMR_STATUS_STALE;
            }
            if (oamr_retained_sequence_compare(
                    newer.metadata.sequence,
                    older.metadata.sequence) < 0) {
                target = index;
            }
        }
    }

    sequence = retained->next_sequence;
    __asm volatile ("" : : : "memory");
    *oamr_retained_state_cell(retained, target) =
        (uint32_t)OAMR_FAULT_RETAINED_STATE_WRITING;
    __asm volatile ("" : : : "memory");
    oamr_retained_observe(retained, target,
        OAMR_FAULT_RETAINED_TEST_MARK_WRITING);

    oamr_retained_write_metadata(retained, target, sequence, metadata);
    oamr_retained_observe(retained, target,
        OAMR_FAULT_RETAINED_TEST_METADATA_WRITTEN);
    oamr_retained_write_bytes(retained, target,
        OAMR_FAULT_RETAINED_PAYLOAD_OFFSET, encoded, sizeof(encoded));
    oamr_retained_observe(retained, target,
        OAMR_FAULT_RETAINED_TEST_PAYLOAD_WRITTEN);

    crc = oamr_fault_crc32(encoded, sizeof(encoded));
    oamr_retained_put_u32(encoded, crc);
    oamr_retained_write_bytes(retained, target,
        OAMR_FAULT_RETAINED_PAYLOAD_CRC_OFFSET, encoded, sizeof(uint32_t));
    oamr_retained_observe(retained, target,
        OAMR_FAULT_RETAINED_TEST_PAYLOAD_CRC_WRITTEN);

    crc = oamr_retained_commit_crc(retained, target);
    oamr_retained_put_u32(encoded, crc);
    oamr_retained_write_bytes(retained, target,
        OAMR_FAULT_RETAINED_COMMIT_CRC_OFFSET, encoded, sizeof(uint32_t));
    oamr_retained_observe(retained, target,
        OAMR_FAULT_RETAINED_TEST_COMMIT_CRC_WRITTEN);

    /* Publish payload and integrity data before the state marker. */
    __asm volatile ("" : : : "memory");
    OAMR_RETAINED_DMB();
    *oamr_retained_state_cell(retained, target) =
        (uint32_t)OAMR_FAULT_RETAINED_STATE_COMMITTED;
    __asm volatile ("" : : : "memory");
    oamr_retained_observe(retained, target,
        OAMR_FAULT_RETAINED_TEST_COMMITTED);
    retained->next_sequence = sequence + 1u;
    if (out_sequence != NULL) {
        *out_sequence = sequence;
    }
    return OAMR_STATUS_OK;
}

oamr_status_t oamr_fault_retained_load(
    const oamr_fault_retained_t *retained,
    oamr_fault_retained_snapshot_t *out_snapshot)
{
    oamr_status_t status;

    if (out_snapshot != NULL) {
        (void)memset(out_snapshot, 0, sizeof(*out_snapshot));
    }
    if (retained == NULL || retained->storage == NULL ||
        out_snapshot == NULL) {
        return OAMR_STATUS_INVALID;
    }
    status = oamr_retained_load_any(retained, out_snapshot);
    if (status != OAMR_STATUS_OK) {
        (void)memset(out_snapshot, 0, sizeof(*out_snapshot));
    }
    return status;
}

oamr_status_t oamr_fault_retained_retire(
    oamr_fault_retained_t *retained,
    size_t slot_index,
    const oamr_fault_retained_snapshot_t *snapshot)
{
    oamr_fault_retained_snapshot_t current;
    bool found = false;
    oamr_status_t status;

    if (retained == NULL || retained->storage == NULL || snapshot == NULL) {
        return OAMR_STATUS_INVALID;
    }
    if (slot_index >= OAMR_FAULT_RETAINED_SLOT_COUNT) {
        return OAMR_STATUS_INVALID;
    }
    status = oamr_retained_validate_slot(
        retained, slot_index,
        (uint32_t)OAMR_FAULT_RETAINED_STATE_COMMITTED,
        &current, &found);
    if (status != OAMR_STATUS_OK || !found) {
        return status == OAMR_STATUS_STALE ? OAMR_STATUS_STALE : OAMR_STATUS_EMPTY;
    }
    if (current.record.crc32 != snapshot->record.crc32 ||
        current.metadata.sequence != snapshot->metadata.sequence ||
        current.metadata.exc_return != snapshot->metadata.exc_return ||
        current.metadata.exception_kind != snapshot->metadata.exception_kind ||
        current.metadata.flags != snapshot->metadata.flags ||
        memcmp(&current.record.data, &snapshot->record.data,
               sizeof(current.record.data)) != 0) {
        return OAMR_STATUS_EMPTY;
    }

    /* Revalidate exact content and stable state immediately before retirement. */
    status = oamr_retained_validate_slot(
        retained, slot_index,
        (uint32_t)OAMR_FAULT_RETAINED_STATE_COMMITTED,
        &current, &found);
    if (status != OAMR_STATUS_OK || !found) {
        return OAMR_STATUS_STALE;
    }
    if (current.record.crc32 != snapshot->record.crc32 ||
        current.metadata.sequence != snapshot->metadata.sequence ||
        current.metadata.exc_return != snapshot->metadata.exc_return ||
        current.metadata.exception_kind != snapshot->metadata.exception_kind ||
        current.metadata.flags != snapshot->metadata.flags ||
        memcmp(&current.record.data, &snapshot->record.data,
               sizeof(current.record.data)) != 0) {
        return OAMR_STATUS_STALE;
    }
    __asm volatile ("" : : : "memory");
    OAMR_RETAINED_DMB();
    *oamr_retained_state_cell(retained, slot_index) =
        (uint32_t)OAMR_FAULT_RETAINED_STATE_RETIRED;
    __asm volatile ("" : : : "memory");
    oamr_retained_observe(retained, slot_index,
        OAMR_FAULT_RETAINED_TEST_RETIRED);
    return OAMR_STATUS_OK;
}
