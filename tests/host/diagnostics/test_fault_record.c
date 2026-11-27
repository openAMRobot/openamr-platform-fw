/* SPDX-License-Identifier: MIT */
#include "test_harness.h"

#include "oamr/fault_record.h"

#include <stdint.h>
#include <string.h>

#define GUARD_BYTES ((size_t)8u)
#define ENCODE_BUFFER_SIZE (OAMR_FAULT_RECORD_SERIALIZED_SIZE + (2u * GUARD_BYTES))

static oamr_fault_record_data_t sample_data(void)
{
    oamr_fault_record_data_t data;

    data.raw_reset_flags = 0u;
    data.stacked_pc = UINT32_C(0x80000001);
    data.stacked_lr = UINT32_C(0xfffffff9);
    data.stacked_xpsr = UINT32_C(0x01000000);
    data.cfsr = UINT32_C(0x00008200);
    data.hfsr = UINT32_C(0x40000000);
    data.bfar = UINT32_C(0xe000ed38);
    data.mmar = UINT32_C(0xffffffff);
    return data;
}

static oamr_reset_flag_definitions_t sample_definitions(void)
{
    oamr_reset_flag_definitions_t definitions;

    definitions.power_on = UINT32_C(0x00800000);
    definitions.pin = UINT32_C(0x00400000);
    definitions.software = UINT32_C(0x01000000);
    definitions.iwdg = UINT32_C(0x04000000);
    definitions.wwdg = UINT32_C(0x10000000);
    definitions.low_power = UINT32_C(0x40000000);
    definitions.bor = UINT32_C(0x00200000);
    definitions.other = UINT32_C(0x00020000);
    return definitions;
}

static void check_zero_record(const oamr_fault_record_t *record)
{
    static const oamr_fault_record_t zero_record = {0};

    CHECK(memcmp(record, &zero_record, sizeof(*record)) == 0);
}

static void expect_invalid(const uint8_t *data, size_t size,
                           oamr_fault_validate_error_t error,
                           size_t inspected_size)
{
    oamr_fault_validation_t validation;

    memset(&validation, 0xa5, sizeof(validation));
    CHECK(oamr_fault_record_validate(data, size, &validation) ==
          OAMR_FAULT_RECORD_INVALID);
    CHECK(validation.validity == OAMR_FAULT_RECORD_INVALID);
    CHECK(validation.error == error);
    CHECK(validation.inspected_size == inspected_size);
}

static void test_crc_standard_and_empty_vectors(void)
{
    static const char vector[] = "123456789";
    const uint8_t empty = 0u;

    CHECK(OAMR_FAULT_RECORD_SERIALIZED_SIZE == 44u);
    CHECK(OAMR_FAULT_RECORD_CRC_OFFSET == 40u);
    CHECK(oamr_fault_crc32(vector, sizeof(vector) - 1u) == UINT32_C(0xcbf43926));
    CHECK(oamr_fault_crc32(&empty, 0u) == 0u);
    CHECK(oamr_fault_crc32(NULL, 0u) == 0u);
    CHECK(oamr_fault_crc32(NULL, 9u) == 0u);
}

static void test_init_layout_and_little_endian_fields(void)
{
    oamr_fault_record_t record;
    oamr_fault_record_data_t data = sample_data();
    oamr_reset_flag_definitions_t definitions = sample_definitions();
    uint8_t encoded[ENCODE_BUFFER_SIZE];
    size_t encoded_size = 99u;
    static const uint8_t expected_first_40[40] = {
        0x52u, 0x46u, 0x41u, 0x4fu,
        0x01u, 0x00u,
        0x2cu, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x01u, 0x00u, 0x00u, 0x80u,
        0xf9u, 0xffu, 0xffu, 0xffu,
        0x00u, 0x00u, 0x00u, 0x01u,
        0x00u, 0x82u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x40u,
        0x38u, 0xedu, 0x00u, 0xe0u,
        0xffu, 0xffu, 0xffu, 0xffu
    };
    size_t index;

    memset(encoded, 0x5a, sizeof(encoded));
    CHECK(oamr_fault_record_init(&record, &data, &definitions) == OAMR_STATUS_OK);
    CHECK(record.magic == OAMR_FAULT_RECORD_MAGIC);
    CHECK(record.version == OAMR_FAULT_RECORD_VERSION);
    CHECK(record.length == OAMR_FAULT_RECORD_SERIALIZED_SIZE);
    CHECK(memcmp(&record.data, &data, sizeof(data)) == 0);
    CHECK(memcmp(&record.reset_definitions, &definitions, sizeof(definitions)) == 0);
    CHECK(oamr_fault_record_encode(&record, encoded + GUARD_BYTES,
                                   OAMR_FAULT_RECORD_SERIALIZED_SIZE + 4u,
                                   &encoded_size) == OAMR_STATUS_OK);
    CHECK(encoded_size == OAMR_FAULT_RECORD_SERIALIZED_SIZE);
    CHECK(memcmp(encoded + GUARD_BYTES, expected_first_40,
                 sizeof(expected_first_40)) == 0);
    CHECK(oamr_fault_crc32(encoded + GUARD_BYTES,
                           OAMR_FAULT_RECORD_CRC_OFFSET) == record.crc32);
    for (index = 0u; index < 4u; ++index) {
        CHECK(encoded[GUARD_BYTES + 40u + index] ==
              (uint8_t)(record.crc32 >> (8u * index)));
    }
    for (index = 0u; index < GUARD_BYTES; ++index) {
        CHECK(encoded[index] == 0x5au);
        CHECK(encoded[GUARD_BYTES + 44u + index] == 0x5au);
    }
    /* The encoder must not append the in-memory reset definitions. */
    CHECK(encoded[GUARD_BYTES + 44u] == 0x5au);
}

static void test_unaligned_decode_and_large_input(void)
{
    oamr_fault_record_t original;
    oamr_fault_record_t decoded;
    oamr_fault_record_data_t data = sample_data();
    oamr_reset_flag_definitions_t definitions = sample_definitions();
    oamr_reset_flag_definitions_t decoded_definitions;
    uint8_t storage[OAMR_FAULT_RECORD_SERIALIZED_SIZE + 6u];
    size_t encoded_size = 0u;
    oamr_fault_validation_t validation;

    CHECK(oamr_fault_record_init(&original, &data, &definitions) == OAMR_STATUS_OK);
    CHECK(oamr_fault_record_encode(&original, storage + 1u,
                                   sizeof(storage) - 1u, &encoded_size) ==
          OAMR_STATUS_OK);
    storage[45] = 0xdeu;
    storage[46] = 0xadu;
    memset(&validation, 0, sizeof(validation));
    CHECK(oamr_fault_record_validate(storage + 1u, 46u, &validation) ==
          OAMR_FAULT_RECORD_VALID);
    CHECK(validation.error == 0);
    CHECK(validation.inspected_size == 44u);
    CHECK(oamr_fault_record_decode(storage + 1u, 46u, &decoded) == OAMR_STATUS_OK);
    CHECK(decoded.magic == original.magic);
    CHECK(decoded.version == original.version);
    CHECK(decoded.length == original.length);
    CHECK(memcmp(&decoded.data, &data, sizeof(data)) == 0);
    CHECK(decoded.crc32 == original.crc32);
    memset(&decoded_definitions, 0, sizeof(decoded_definitions));
    CHECK(memcmp(&decoded.reset_definitions, &decoded_definitions,
                 sizeof(decoded_definitions)) == 0);
}

static void test_one_bit_corruption_at_every_byte(void)
{
    oamr_fault_record_t record;
    oamr_fault_record_t decoded;
    oamr_fault_record_data_t data = sample_data();
    oamr_reset_flag_definitions_t definitions = sample_definitions();
    uint8_t encoded[OAMR_FAULT_RECORD_SERIALIZED_SIZE];
    size_t encoded_size = 0u;
    size_t offset;

    CHECK(oamr_fault_record_init(&record, &data, &definitions) == OAMR_STATUS_OK);
    CHECK(oamr_fault_record_encode(&record, encoded, sizeof(encoded),
                                   &encoded_size) == OAMR_STATUS_OK);

    for (offset = 0u; offset < OAMR_FAULT_RECORD_SERIALIZED_SIZE; ++offset) {
        oamr_fault_validate_error_t expected_error;

        encoded[offset] ^= UINT8_C(0x01);
        if (offset < 4u) {
            expected_error = OAMR_FAULT_VALIDATE_BAD_MAGIC;
        } else if (offset < 6u) {
            expected_error = OAMR_FAULT_VALIDATE_UNSUPPORTED_VERSION;
        } else if (offset < 8u) {
            expected_error = OAMR_FAULT_VALIDATE_BAD_LENGTH;
        } else {
            expected_error = OAMR_FAULT_VALIDATE_CRC_MISMATCH;
        }
        expect_invalid(encoded, sizeof(encoded), expected_error, 44u);
        memset(&decoded, 0xa5, sizeof(decoded));
        CHECK(oamr_fault_record_decode(encoded, sizeof(encoded), &decoded) ==
              OAMR_STATUS_INVALID);
        check_zero_record(&decoded);
        encoded[offset] ^= UINT8_C(0x01);
    }
    CHECK(oamr_fault_record_validate(encoded, sizeof(encoded), NULL) ==
          OAMR_FAULT_RECORD_VALID);
}

static void test_header_corruption_and_truncation_reasons(void)
{
    oamr_fault_record_t record;
    oamr_fault_record_data_t data = sample_data();
    oamr_reset_flag_definitions_t definitions = sample_definitions();
    uint8_t encoded[OAMR_FAULT_RECORD_SERIALIZED_SIZE];
    static const size_t truncated_sizes[] = {0u, 1u, 7u, 8u, 39u, 40u, 43u};
    size_t encoded_size = 0u;
    size_t index;

    CHECK(oamr_fault_record_init(&record, &data, &definitions) == OAMR_STATUS_OK);
    CHECK(oamr_fault_record_encode(&record, encoded, sizeof(encoded),
                                   &encoded_size) == OAMR_STATUS_OK);

    encoded[2] ^= 0x80u;
    expect_invalid(encoded, sizeof(encoded), OAMR_FAULT_VALIDATE_BAD_MAGIC, 44u);
    encoded[2] ^= 0x80u;
    encoded[5] = 1u;
    expect_invalid(encoded, sizeof(encoded),
                   OAMR_FAULT_VALIDATE_UNSUPPORTED_VERSION, 44u);
    encoded[5] = 0u;
    encoded[6] = 43u;
    expect_invalid(encoded, sizeof(encoded), OAMR_FAULT_VALIDATE_BAD_LENGTH, 44u);
    encoded[6] = 44u;
    encoded[39] ^= 0x01u;
    expect_invalid(encoded, sizeof(encoded), OAMR_FAULT_VALIDATE_CRC_MISMATCH, 44u);
    encoded[39] ^= 0x01u;
    encoded[43] ^= 0x80u;
    expect_invalid(encoded, sizeof(encoded), OAMR_FAULT_VALIDATE_CRC_MISMATCH, 44u);
    encoded[43] ^= 0x80u;

    for (index = 0u; index < sizeof(truncated_sizes) / sizeof(truncated_sizes[0]);
         ++index) {
        expect_invalid(encoded, truncated_sizes[index],
                       OAMR_FAULT_VALIDATE_TRUNCATED, truncated_sizes[index]);
    }
    expect_invalid(NULL, 0u, OAMR_FAULT_VALIDATE_TRUNCATED, 0u);
    expect_invalid(NULL, 1u, OAMR_FAULT_VALIDATE_MALFORMED, 1u);
    expect_invalid(NULL, 43u, OAMR_FAULT_VALIDATE_MALFORMED, 43u);
    expect_invalid(NULL, 44u, OAMR_FAULT_VALIDATE_MALFORMED, 0u);
    expect_invalid(NULL, 45u, OAMR_FAULT_VALIDATE_MALFORMED, 0u);
}

static void test_invalid_init_and_encode_arguments(void)
{
    oamr_fault_record_t record;
    oamr_fault_record_t untouched;
    oamr_fault_record_data_t data = sample_data();
    oamr_reset_flag_definitions_t definitions = sample_definitions();
    uint8_t output[45];
    size_t output_size;

    CHECK(oamr_fault_record_init(NULL, &data, &definitions) == OAMR_STATUS_INVALID);
    memset(&record, 0xa5, sizeof(record));
    CHECK(oamr_fault_record_init(&record, NULL, &definitions) == OAMR_STATUS_INVALID);
    CHECK(record.magic == 0u);
    CHECK(record.version == 0u);
    CHECK(record.length == 0u);
    CHECK(record.crc32 == 0u);
    memset(&record, 0xa5, sizeof(record));
    CHECK(oamr_fault_record_init(&record, &data, NULL) == OAMR_STATUS_INVALID);
    CHECK(record.magic == 0u);
    CHECK(record.version == 0u);
    CHECK(record.length == 0u);
    CHECK(record.crc32 == 0u);

    CHECK(oamr_fault_record_init(&record, &data, &definitions) == OAMR_STATUS_OK);
    output_size = 123u;
    CHECK(oamr_fault_record_encode(NULL, output, sizeof(output), &output_size) ==
          OAMR_STATUS_INVALID);
    CHECK(output_size == 0u);
    output_size = 123u;
    CHECK(oamr_fault_record_encode(&record, NULL, sizeof(output), &output_size) ==
          OAMR_STATUS_INVALID);
    CHECK(output_size == 0u);
    CHECK(oamr_fault_record_encode(&record, output, sizeof(output), NULL) ==
          OAMR_STATUS_INVALID);
    memset(output, 0x5a, sizeof(output));
    output_size = 123u;
    CHECK(oamr_fault_record_encode(&record, output, 43u, &output_size) ==
          OAMR_STATUS_INVALID);
    CHECK(output_size == 0u);
    CHECK(output[43] == 0x5au);

    memset(&untouched, 0xa5, sizeof(untouched));
    CHECK(oamr_fault_record_decode(output, 44u, NULL) == OAMR_STATUS_INVALID);
    CHECK(oamr_fault_record_decode(output, 44u, &untouched) == OAMR_STATUS_INVALID);
    check_zero_record(&untouched);
}

int main(void)
{
    RUN(test_crc_standard_and_empty_vectors);
    RUN(test_init_layout_and_little_endian_fields);
    RUN(test_unaligned_decode_and_large_input);
    RUN(test_one_bit_corruption_at_every_byte);
    RUN(test_header_corruption_and_truncation_reasons);
    RUN(test_invalid_init_and_encode_arguments);
    return TEST_END();
}
