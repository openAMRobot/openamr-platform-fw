/* SPDX-License-Identifier: MIT */
#include "test_harness.h"

#include "oamr/fault_retained.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define MAX_CAPTURED_IMAGES 128u

typedef struct {
    oamr_fault_retained_storage_t before;
    oamr_fault_retained_storage_t images[MAX_CAPTURED_IMAGES];
    size_t count;
    bool capturing;
    bool fail_after_first_state;
    bool failed_state;
} observer_state_t;

static observer_state_t observer;

static oamr_reset_flag_definitions_t definitions(void)
{
    oamr_reset_flag_definitions_t result;
    (void)memset(&result, 0, sizeof(result));
    result.power_on = UINT32_C(0x00800000);
    result.pin = UINT32_C(0x00400000);
    result.software = UINT32_C(0x01000000);
    result.iwdg = UINT32_C(0x04000000);
    result.wwdg = UINT32_C(0x10000000);
    result.low_power = UINT32_C(0x40000000);
    result.bor = UINT32_C(0x00200000);
    result.other = UINT32_C(0x00020000);
    return result;
}

static oamr_fault_record_t record(uint32_t marker)
{
    oamr_fault_record_t result;
    oamr_fault_record_data_t data;
    oamr_reset_flag_definitions_t masks = definitions();

    (void)memset(&data, 0, sizeof(data));
    data.raw_reset_flags = marker;
    data.stacked_pc = UINT32_C(0x80000001) + marker;
    data.stacked_lr = OAMR_EXC_RETURN_THREAD_PSP_BASIC;
    data.stacked_xpsr = UINT32_C(0x01000000);
    data.cfsr = OAMR_EXCEPTION_CFSR_BFARVALID;
    data.hfsr = marker + 1u;
    data.bfar = UINT32_C(0x10000000) + marker;
    data.mmar = UINT32_C(0x20000000) + marker;
    CHECK(oamr_fault_record_init(&result, &data, &masks) == OAMR_STATUS_OK);
    return result;
}

static oamr_exception_capture_metadata_t metadata(uint16_t kind)
{
    oamr_exception_capture_metadata_t result;
    result.sequence = UINT32_C(0xa5a5a5a5); /* Retention owns this field. */
    result.exc_return = OAMR_EXC_RETURN_THREAD_PSP_BASIC;
    result.exception_kind = kind;
    result.flags = OAMR_EXCEPTION_CAPTURE_STACKED_VALID |
                   OAMR_EXCEPTION_CAPTURE_USES_PSP |
                   OAMR_EXCEPTION_CAPTURE_BFAR_VALID;
    return result;
}

static oamr_status_t commit(oamr_fault_retained_t *retained,
                            uint32_t marker,
                            const oamr_exception_capture_metadata_t *meta,
                            uint32_t *sequence)
{
    oamr_fault_record_t input = record(marker);
    return oamr_fault_retained_commit(retained, &input, meta, sequence);
}

static void init(oamr_fault_retained_t *retained,
                 oamr_fault_retained_storage_t *storage)
{
    (void)memset(storage, 0, sizeof(*storage));
    CHECK(oamr_fault_retained_init(retained, storage) == OAMR_STATUS_OK);
}

static bool snapshot_equal(const oamr_fault_retained_snapshot_t *left,
                           const oamr_fault_retained_snapshot_t *right)
{
    oamr_fault_record_t left_record = left->record;
    oamr_fault_record_t right_record = right->record;
    (void)memset(&left_record.reset_definitions, 0,
                 sizeof(left_record.reset_definitions));
    (void)memset(&right_record.reset_definitions, 0,
                 sizeof(right_record.reset_definitions));
    return memcmp(&left_record, &right_record, sizeof(left_record)) == 0 &&
           memcmp(&left->metadata, &right->metadata,
                  sizeof(left->metadata)) == 0;
}

static oamr_status_t retire_loaded(oamr_fault_retained_t *retained,
                                   oamr_status_t expected_result)
{
    oamr_fault_retained_snapshot_t snapshot;
    oamr_status_t status = oamr_fault_retained_load(retained, &snapshot);
    oamr_status_t result;

    if (status != OAMR_STATUS_OK) {
        result = status;
    } else {
        result = oamr_fault_retained_retire(retained, snapshot.slot_index, &snapshot);
    }
    CHECK(result == expected_result);
    return result;
}

static oamr_status_t load_expect(oamr_fault_retained_t *retained,
                                 oamr_status_t expected,
                                 uint32_t sequence,
                                 uint32_t expected_marker)
{
    oamr_fault_retained_snapshot_t snapshot;
    oamr_status_t status;

    (void)memset(&snapshot, 0xa5, sizeof(snapshot));
    status = oamr_fault_retained_load(retained, &snapshot);
    CHECK(status == expected);
    if (status == OAMR_STATUS_OK) {
        CHECK(snapshot.metadata.sequence == sequence);
        /* Marker is supplied independently of the retention-assigned sequence. */
        CHECK(snapshot.record.data.raw_reset_flags == expected_marker);
        CHECK(snapshot.metadata.exception_kind ==
              (uint16_t)OAMR_EXCEPTION_KIND_BUS_FAULT);
        CHECK((snapshot.metadata.flags &
               OAMR_EXCEPTION_CAPTURE_STACKED_VALID) != 0u);
    } else {
        static const oamr_fault_retained_snapshot_t zero = {0};
        CHECK(memcmp(&snapshot, &zero, sizeof(snapshot)) == 0);
    }
    return status;
}

void oamr_fault_retained_test_observe(
    const oamr_fault_retained_storage_t *storage,
    size_t slot_index,
    oamr_fault_retained_test_stage_t stage)
{
    (void)slot_index;
    if (observer.capturing && observer.count < MAX_CAPTURED_IMAGES) {
        (void)memcpy(&observer.images[observer.count], storage,
                     sizeof(*storage));
        observer.count += 1u;
    }
    if (observer.fail_after_first_state &&
        stage == OAMR_FAULT_RETAINED_TEST_AFTER_STATE_READ &&
        !observer.failed_state) {
        *(volatile uint32_t *)(void *)
            &storage->bytes[OAMR_FAULT_RETAINED_STATE_OFFSET] = (uint32_t)
            OAMR_FAULT_RETAINED_STATE_WRITING;
        observer.failed_state = true;
    }
}

static void test_blank_and_single_commit_load_retire(void)
{
    oamr_fault_retained_t retained;
    oamr_fault_retained_storage_t storage;
    oamr_fault_record_t first = record(1u);
    oamr_exception_capture_metadata_t meta = metadata(
        OAMR_EXCEPTION_KIND_BUS_FAULT);
    oamr_fault_retained_snapshot_t loaded;
    uint32_t sequence = 99u;
    oamr_reset_flag_definitions_t zero_masks;

    init(&retained, &storage);
    CHECK(load_expect(&retained, OAMR_STATUS_EMPTY, 0u, 0u) == OAMR_STATUS_EMPTY);
    CHECK(oamr_fault_retained_commit(&retained, &first, &meta, &sequence) ==
          OAMR_STATUS_OK);
    CHECK(sequence == 1u);
    CHECK(meta.sequence == UINT32_C(0xa5a5a5a5));
    CHECK(load_expect(&retained, OAMR_STATUS_OK, sequence, 1u) == OAMR_STATUS_OK);
    CHECK(oamr_fault_retained_load(&retained, &loaded) == OAMR_STATUS_OK);
    CHECK(memcmp(&loaded.record.data, &first.data, sizeof(first.data)) == 0);
    CHECK(loaded.record.crc32 == first.crc32);
    (void)memset(&zero_masks, 0, sizeof(zero_masks));
    CHECK(memcmp(&loaded.record.reset_definitions, &zero_masks,
                 sizeof(zero_masks)) == 0);
    CHECK(oamr_fault_retained_retire(&retained, 1u, &loaded) ==
          OAMR_STATUS_EMPTY);
    CHECK(load_expect(&retained, OAMR_STATUS_OK, sequence, 1u) == OAMR_STATUS_OK);
    CHECK(retire_loaded(&retained, OAMR_STATUS_OK) == OAMR_STATUS_OK);
    CHECK(load_expect(&retained, OAMR_STATUS_EMPTY, 0u, 0u) == OAMR_STATUS_EMPTY);
    CHECK(retire_loaded(&retained, OAMR_STATUS_EMPTY) == OAMR_STATUS_EMPTY);
    CHECK(oamr_fault_retained_commit(&retained, &first, &meta, NULL) ==
          OAMR_STATUS_OK);
    CHECK(load_expect(&retained, OAMR_STATUS_OK, 2u, 1u) == OAMR_STATUS_OK);
}

static void test_repeated_faults_and_boot_recovery(void)
{
    oamr_fault_retained_t retained;
    oamr_fault_retained_t rebooted;
    oamr_fault_retained_storage_t storage;
    oamr_fault_retained_snapshot_t previous;
    oamr_fault_retained_snapshot_t latest;
    oamr_exception_capture_metadata_t meta = metadata(
        OAMR_EXCEPTION_KIND_BUS_FAULT);
    uint32_t first_sequence = 0u;
    uint32_t sequence = 0u;
    uint32_t index;

    init(&retained, &storage);
    for (index = 1u; index <= 8u; ++index) {
        oamr_fault_record_t current = record(index);
        CHECK(oamr_fault_retained_commit(&retained, &current, &meta,
                                         &sequence) == OAMR_STATUS_OK);
        CHECK(sequence == index);
        CHECK(load_expect(&retained, OAMR_STATUS_OK, index, index) == OAMR_STATUS_OK);
        if (index == 7u) {
            CHECK(oamr_fault_retained_load(&retained, &previous) ==
                  OAMR_STATUS_OK);
        }
        if (index == 4u) {
            first_sequence = sequence;
        }
    }
    CHECK(first_sequence == 4u);
    CHECK(oamr_fault_retained_init(&rebooted, &storage) == OAMR_STATUS_OK);
    CHECK(oamr_fault_retained_load(&rebooted, &latest) == OAMR_STATUS_OK);
    CHECK(latest.metadata.sequence == 8u);
    CHECK(rebooted.next_sequence == 9u);
    CHECK(retire_loaded(&rebooted, OAMR_STATUS_OK) == OAMR_STATUS_OK);
    CHECK(oamr_fault_retained_load(&rebooted, &latest) == OAMR_STATUS_OK);
    CHECK(snapshot_equal(&latest, &previous));
    CHECK(oamr_fault_retained_init(&rebooted, &storage) == OAMR_STATUS_OK);
    CHECK(commit(&rebooted, 9u, &meta, &sequence) ==
          OAMR_STATUS_OK);
    CHECK(sequence == 9u);
}

static void test_only_second_slot_and_tie_order(void)
{
    oamr_fault_retained_t retained;
    oamr_fault_retained_t boot;
    oamr_fault_retained_storage_t storage;
    oamr_exception_capture_metadata_t meta = metadata(
        OAMR_EXCEPTION_KIND_BUS_FAULT);
    uint32_t sequence = 0u;
    oamr_fault_retained_snapshot_t snapshot;

    init(&retained, &storage);
    CHECK(commit(&retained, 2u, &meta, &sequence) == OAMR_STATUS_OK);
    CHECK(sequence == 1u);
    CHECK(oamr_fault_retained_load(&retained, &snapshot) == OAMR_STATUS_OK);
    CHECK(snapshot.slot_index == 0u);

    /* Retention assigns strictly increasing sequences, so two commits can never
     * produce a tie. Build a genuine equal-sequence state by duplicating the
     * committed slot-zero image into slot one: both slots are then valid with
     * the same sequence, payload and metadata. */
    (void)memcpy(&storage.bytes[OAMR_FAULT_RETAINED_SLOT_BYTES],
                 &storage.bytes[0], OAMR_FAULT_RETAINED_SLOT_BYTES);

    /* Documented tie rule: equal sequences select slot zero. */
    CHECK(oamr_fault_retained_load(&retained, &snapshot) == OAMR_STATUS_OK);
    CHECK(snapshot.metadata.sequence == 1u);
    CHECK(snapshot.record.data.raw_reset_flags == 2u);
    CHECK(snapshot.slot_index == 0u);
    CHECK(oamr_fault_retained_retire(&retained, snapshot.slot_index, &snapshot) ==
          OAMR_STATUS_OK);

    /* Only the second slot remains valid and is independently loadable. */
    CHECK(oamr_fault_retained_load(&retained, &snapshot) == OAMR_STATUS_OK);
    CHECK(snapshot.metadata.sequence == 1u);
    CHECK(snapshot.record.data.raw_reset_flags == 2u);
    CHECK(snapshot.slot_index == 1u);
    CHECK(oamr_fault_retained_retire(&retained, snapshot.slot_index, &snapshot) ==
          OAMR_STATUS_OK);
    CHECK(load_expect(&retained, OAMR_STATUS_EMPTY, 0u, 0u) == OAMR_STATUS_EMPTY);

    /* Retired slots still carry sequence 1 for recovery at the next boot. */
    CHECK(oamr_fault_retained_init(&boot, &storage) == OAMR_STATUS_OK);
    CHECK(boot.next_sequence == 2u);
    CHECK(commit(&boot, 3u, &meta, &sequence) == OAMR_STATUS_OK);
    CHECK(sequence == 2u);
}

static void test_sequence_wrap_and_retired_sequence_recovery(void)
{
    oamr_fault_retained_t retained;
    oamr_fault_retained_t boot;
    oamr_fault_retained_storage_t storage;
    oamr_exception_capture_metadata_t meta = metadata(
        OAMR_EXCEPTION_KIND_BUS_FAULT);
    uint32_t sequence = 0u;

    init(&retained, &storage);
    retained.next_sequence = UINT32_MAX;
    CHECK(commit(&retained, UINT32_MAX - 1u, &meta, &sequence) ==
          OAMR_STATUS_OK);
    CHECK(sequence == UINT32_MAX);
    CHECK(load_expect(&retained, OAMR_STATUS_OK, UINT32_MAX, UINT32_MAX - 1u) == OAMR_STATUS_OK);
    CHECK(commit(&retained, UINT32_MAX, &meta, &sequence) ==
          OAMR_STATUS_OK);
    CHECK(sequence == 0u);
    CHECK(load_expect(&retained, OAMR_STATUS_OK, 0u, UINT32_MAX) == OAMR_STATUS_OK);
    CHECK(oamr_fault_retained_init(&boot, &storage) == OAMR_STATUS_OK);
    CHECK(boot.next_sequence == 1u);
    CHECK(commit(&boot, 1u, &meta, &sequence) ==
          OAMR_STATUS_OK);
    CHECK(sequence == 1u);
    CHECK(load_expect(&boot, OAMR_STATUS_OK, 1u, 1u) == OAMR_STATUS_OK);

    /* With only the wrapped record retired, recovery advances from it. */
    CHECK(retire_loaded(&boot, OAMR_STATUS_OK) == OAMR_STATUS_OK);
    CHECK(oamr_fault_retained_init(&boot, &storage) == OAMR_STATUS_OK);
    /* Recovery begins after the newest retained record, including retired
     * slots; it is not derived solely from the newest still-pending record. */
    CHECK(boot.next_sequence == 2u);
    CHECK(retire_loaded(&boot, OAMR_STATUS_OK) == OAMR_STATUS_OK);
    CHECK(oamr_fault_retained_init(&boot, &storage) == OAMR_STATUS_OK);
    CHECK(boot.next_sequence == 2u);
    CHECK(load_expect(&boot, OAMR_STATUS_EMPTY, 0u, 0u) == OAMR_STATUS_EMPTY);
}

static void test_invalid_arguments_and_records(void)
{
    oamr_fault_retained_t retained;
    oamr_fault_retained_t missing = {0};
    oamr_fault_retained_storage_t storage;
    oamr_fault_retained_snapshot_t snapshot;
    oamr_fault_record_t valid = record(1u);
    oamr_fault_record_t invalid;
    oamr_exception_capture_metadata_t meta = metadata(
        OAMR_EXCEPTION_KIND_BUS_FAULT);
    uint32_t sequence = 55u;
    oamr_fault_retained_storage_t before;

    CHECK(oamr_fault_retained_init(NULL, &storage) == OAMR_STATUS_INVALID);
    CHECK(oamr_fault_retained_init(&retained, NULL) == OAMR_STATUS_INVALID);
    init(&retained, &storage);
    (void)memset(&snapshot, 0xa5, sizeof(snapshot));
    CHECK(oamr_fault_retained_load(NULL, &snapshot) == OAMR_STATUS_INVALID);
    CHECK(oamr_fault_retained_load(&missing, &snapshot) == OAMR_STATUS_INVALID);
    CHECK(oamr_fault_retained_load(&retained, NULL) == OAMR_STATUS_INVALID);
    CHECK(oamr_fault_retained_retire(NULL, 0u, &snapshot) == OAMR_STATUS_INVALID);
    CHECK(oamr_fault_retained_retire(&missing, 0u, &snapshot) == OAMR_STATUS_INVALID);
    CHECK(oamr_fault_retained_commit(NULL, &valid, &meta, &sequence) ==
          OAMR_STATUS_INVALID);
    CHECK(sequence == 0u);
    sequence = 55u;
    CHECK(oamr_fault_retained_commit(&missing, &valid, &meta, &sequence) ==
          OAMR_STATUS_INVALID);
    CHECK(sequence == 0u);
    CHECK(oamr_fault_retained_commit(&retained, NULL, &meta, NULL) ==
          OAMR_STATUS_INVALID);
    CHECK(oamr_fault_retained_commit(&retained, &valid, NULL, NULL) ==
          OAMR_STATUS_INVALID);
    (void)memcpy(&before, &storage, sizeof(before));
    (void)memset(&invalid, 0xa5, sizeof(invalid));
    CHECK(oamr_fault_retained_commit(&retained, &invalid, &meta, NULL) ==
          OAMR_STATUS_INVALID);
    invalid = valid;
    invalid.crc32 ^= 1u;
    CHECK(oamr_fault_retained_commit(&retained, &invalid, &meta, NULL) ==
          OAMR_STATUS_INVALID);
    invalid = valid;
    invalid.version += 1u;
    CHECK(oamr_fault_retained_commit(&retained, &invalid, &meta, NULL) ==
          OAMR_STATUS_INVALID);
    CHECK(memcmp(&before, &storage, sizeof(before)) == 0);
}

static void test_corruption_rejection(void)
{
    oamr_fault_retained_t retained;
    oamr_fault_retained_t rebooted;
    oamr_fault_retained_storage_t storage;
    oamr_exception_capture_metadata_t meta = metadata(
        OAMR_EXCEPTION_KIND_BUS_FAULT);
    size_t offset;
    oamr_fault_retained_storage_t good;
    static const size_t offsets[] = {
        OAMR_FAULT_RETAINED_METADATA_OFFSET,
        OAMR_FAULT_RETAINED_METADATA_OFFSET + 4u,
        OAMR_FAULT_RETAINED_METADATA_OFFSET + 8u,
        OAMR_FAULT_RETAINED_PAYLOAD_OFFSET,
        OAMR_FAULT_RETAINED_PAYLOAD_OFFSET + 4u,
        OAMR_FAULT_RETAINED_PAYLOAD_OFFSET + 6u,
        OAMR_FAULT_RETAINED_PAYLOAD_CRC_OFFSET,
        OAMR_FAULT_RETAINED_COMMIT_CRC_OFFSET
    };

    init(&retained, &storage);
    CHECK(commit(&retained, 1u, &meta, NULL) ==
          OAMR_STATUS_OK);
    (void)memcpy(&good, &storage, sizeof(good));

    for (offset = 0u; offset < sizeof(offsets) / sizeof(offsets[0]); ++offset) {
        (void)memcpy(&storage, &good, sizeof(storage));
        storage.bytes[offsets[offset]] ^= 1u;
        CHECK(load_expect(&retained, OAMR_STATUS_INVALID, 0u, 0u) ==
              OAMR_STATUS_INVALID);
        CHECK(oamr_fault_retained_init(&rebooted, &storage) == OAMR_STATUS_OK);
        CHECK(commit(&rebooted, 2u, &meta, NULL) ==
              OAMR_STATUS_OK);
        CHECK(load_expect(&rebooted, OAMR_STATUS_OK, 1u, 2u) == OAMR_STATUS_OK);
    }

    for (offset = 0u; offset < OAMR_FAULT_RETAINED_SLOT_BYTES; ++offset) {
        (void)memset(&storage, 0xa5, sizeof(storage));
        storage.bytes[offset] = 0u;
        CHECK(load_expect(&retained, OAMR_STATUS_INVALID, 0u, 0u) ==
              OAMR_STATUS_INVALID);
    }
}

static void interrupted_images(bool seed_old_record)
{
    oamr_fault_retained_t retained;
    oamr_fault_retained_storage_t storage;
    oamr_exception_capture_metadata_t meta = metadata(
        OAMR_EXCEPTION_KIND_BUS_FAULT);
    oamr_fault_retained_snapshot_t expected;
    bool have_old = false;
    size_t index;

    init(&retained, &storage);
    if (seed_old_record) {
        /* A sole old record occupies slot 1 after boot rebind. */
        CHECK(commit(&retained, 1u, &meta, NULL) == OAMR_STATUS_OK);
        CHECK(commit(&retained, 2u, &meta, NULL) == OAMR_STATUS_OK);
        CHECK(retire_loaded(&retained, OAMR_STATUS_OK) == OAMR_STATUS_OK);
        CHECK(oamr_fault_retained_load(&retained, &expected) == OAMR_STATUS_OK);
        have_old = true;
    }
    (void)memset(&observer, 0, sizeof(observer));
    (void)memcpy(&observer.before, &storage, sizeof(storage));
    observer.capturing = true;
    uint32_t expected_new_sequence = seed_old_record ? 3u : 1u;
    CHECK(commit(&retained, 2u, &meta, NULL) ==
          OAMR_STATUS_OK);
    observer.capturing = false;
    CHECK(observer.count > 60u);
    CHECK(observer.count < MAX_CAPTURED_IMAGES);

    for (index = 0u; index <= observer.count; ++index) {
        oamr_fault_retained_storage_t image;
        oamr_fault_retained_t boot;
        oamr_fault_retained_snapshot_t loaded;
        oamr_status_t status;

        if (index == 0u) {
            (void)memcpy(&image, &observer.before, sizeof(image));
        } else {
            (void)memcpy(&image, &observer.images[index - 1u], sizeof(image));
        }
        CHECK(oamr_fault_retained_init(&boot, &image) == OAMR_STATUS_OK);
        (void)memset(&loaded, 0xa5, sizeof(loaded));
        status = oamr_fault_retained_load(&boot, &loaded);
        if (index == observer.count) {
            /* The completed final commit supersedes the previous record. */
            CHECK(status == OAMR_STATUS_OK);
            if (status == OAMR_STATUS_OK) {
                CHECK(loaded.metadata.sequence == expected_new_sequence);
                CHECK(loaded.record.data.raw_reset_flags == 2u);
            }
        } else if (have_old) {
            CHECK(status == OAMR_STATUS_OK);
            if (status == OAMR_STATUS_OK) {
                CHECK(snapshot_equal(&loaded, &expected));
            }
        } else {
            CHECK(status == OAMR_STATUS_EMPTY);
        }
    }
}

static void test_interrupted_writes_empty_and_with_previous(void)
{
    interrupted_images(false);
    interrupted_images(true);
}

static void test_interrupted_retirement_and_reader_instability(void)
{
    oamr_fault_retained_t retained;
    oamr_fault_retained_t boot;
    oamr_fault_retained_storage_t storage;
    oamr_fault_retained_storage_t valid_image;
    oamr_exception_capture_metadata_t meta = metadata(
        OAMR_EXCEPTION_KIND_BUS_FAULT);
    oamr_fault_retained_snapshot_t snapshot;

    init(&retained, &storage);
    CHECK(commit(&retained, 1u, &meta, NULL) == OAMR_STATUS_OK);
    (void)memcpy(&valid_image, &storage, sizeof(valid_image));

    *(volatile uint32_t *)(void *)&storage.bytes[OAMR_FAULT_RETAINED_STATE_OFFSET] =
        (uint32_t)OAMR_FAULT_RETAINED_STATE_WRITING;
    CHECK(load_expect(&retained, OAMR_STATUS_EMPTY, 0u, 0u) == OAMR_STATUS_EMPTY);

    (void)memcpy(&storage, &valid_image, sizeof(storage));
    CHECK(load_expect(&retained, OAMR_STATUS_OK, 1u, 1u) == OAMR_STATUS_OK);
    CHECK(retire_loaded(&retained, OAMR_STATUS_OK) == OAMR_STATUS_OK);
    CHECK(load_expect(&retained, OAMR_STATUS_EMPTY, 0u, 0u) == OAMR_STATUS_EMPTY);

    CHECK(commit(&retained, 2u, &meta, NULL) == OAMR_STATUS_OK);
    CHECK(load_expect(&retained, OAMR_STATUS_OK, 2u, 2u) == OAMR_STATUS_OK);
    CHECK(oamr_fault_retained_init(&boot, &storage) == OAMR_STATUS_OK);
    CHECK(boot.next_sequence == 3u);

    (void)memset(&observer, 0, sizeof(observer));
    observer.fail_after_first_state = true;
    (void)memset(&snapshot, 0xa5, sizeof(snapshot));
    CHECK(oamr_fault_retained_load(&boot, &snapshot) == OAMR_STATUS_STALE);
    {
        static const oamr_fault_retained_snapshot_t zero = {0};
        CHECK(memcmp(&snapshot, &zero, sizeof(snapshot)) == 0);
    }
}

static void test_valid_slot_survives_corrupt_peer_and_stale_ack(void)
{
    oamr_fault_retained_t retained;
    oamr_fault_retained_t rebooted;
    oamr_fault_retained_storage_t storage;
    oamr_exception_capture_metadata_t meta = metadata(
        OAMR_EXCEPTION_KIND_BUS_FAULT);
    oamr_fault_retained_snapshot_t snapshot;
    oamr_fault_retained_snapshot_t stale;

    init(&retained, &storage);
    CHECK(commit(&retained, 1u, &meta, NULL) == OAMR_STATUS_OK);
    CHECK(commit(&retained, 2u, &meta, NULL) == OAMR_STATUS_OK);
    CHECK(oamr_fault_retained_load(&retained, &snapshot) == OAMR_STATUS_OK);
    stale = snapshot;
    stale.record.crc32 ^= 1u;
    CHECK(oamr_fault_retained_retire(&retained, snapshot.slot_index, &stale) ==
          OAMR_STATUS_EMPTY);

    /* A bad committed peer cannot hide a valid snapshot or trigger replacement
     * of the sole good record. */
    storage.bytes[OAMR_FAULT_RETAINED_PAYLOAD_OFFSET + 1u] ^= 1u;
    CHECK(oamr_fault_retained_load(&retained, &snapshot) == OAMR_STATUS_OK);
    CHECK(snapshot.metadata.sequence == 2u);
    CHECK(snapshot.record.data.raw_reset_flags == 2u);
    CHECK(oamr_fault_retained_init(&rebooted, &storage) == OAMR_STATUS_OK);
    CHECK(oamr_fault_retained_load(&rebooted, &snapshot) == OAMR_STATUS_OK);
    CHECK(snapshot.metadata.sequence == 2u);
}

int main(void)
{
    RUN(test_blank_and_single_commit_load_retire);
    RUN(test_repeated_faults_and_boot_recovery);
    RUN(test_only_second_slot_and_tie_order);
    RUN(test_sequence_wrap_and_retired_sequence_recovery);
    RUN(test_invalid_arguments_and_records);
    RUN(test_corruption_rejection);
    RUN(test_interrupted_writes_empty_and_with_previous);
    RUN(test_interrupted_retirement_and_reader_instability);
    RUN(test_valid_slot_survives_corrupt_peer_and_stale_ack);
    return TEST_END();
}
