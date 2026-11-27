/* SPDX-License-Identifier: MIT */
#include "test_harness.h"

#include "oamr/exception_frame.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TEST_RESET_FLAGS UINT32_C(0xa55a017e)
#define TEST_CFSR UINT32_C(0x82000000)
#define TEST_HFSR UINT32_C(0x40000002)
#define TEST_BFAR UINT32_C(0x12345678)
#define TEST_MMFAR UINT32_C(0x89abcdef)
#define TEST_KIND OAMR_EXCEPTION_KIND_BUS_FAULT

static oamr_exception_scb_snapshot_t sample_scb(void)
{
    oamr_exception_scb_snapshot_t scb;

    scb.cfsr = TEST_CFSR;
    scb.hfsr = TEST_HFSR;
    scb.bfar = TEST_BFAR;
    scb.mmar = TEST_MMFAR;
    return scb;
}

static void fill_frame(uint32_t *words, size_t count)
{
    size_t index;

    for (index = 0u; index < count; ++index) {
        words[index] = UINT32_C(0x51000000) + (uint32_t)index * UINT32_C(0x101);
    }
}

static void expect_zero_data(const oamr_fault_record_data_t *data)
{
    static const oamr_fault_record_data_t zero_data = {0};

    CHECK(memcmp(data, &zero_data, sizeof(zero_data)) == 0);
}

static void expect_zero_metadata(const oamr_exception_capture_metadata_t *metadata)
{
    static const oamr_exception_capture_metadata_t zero_metadata = {0};

    CHECK(memcmp(metadata, &zero_metadata, sizeof(zero_metadata)) == 0);
}

static void expect_raw_snapshot(const oamr_fault_record_data_t *data,
                                const oamr_exception_scb_snapshot_t *scb,
                                uint32_t raw_reset_flags)
{
    CHECK(data->raw_reset_flags == raw_reset_flags);
    CHECK(data->cfsr == scb->cfsr);
    CHECK(data->hfsr == scb->hfsr);
    CHECK(data->bfar == scb->bfar);
    CHECK(data->mmar == scb->mmar);
}

static void expect_invalid_decode(const uint32_t *frame, size_t count,
                                  uint32_t exc_return)
{
    oamr_exception_scb_snapshot_t scb = sample_scb();
    oamr_fault_record_data_t data;
    oamr_exception_capture_metadata_t metadata;

    memset(&data, 0xa5, sizeof(data));
    memset(&metadata, 0xa5, sizeof(metadata));
    CHECK(oamr_exception_frame_decode(frame, count, exc_return, TEST_KIND,
                                      &scb, TEST_RESET_FLAGS, &data, &metadata) ==
          OAMR_STATUS_INVALID);
    expect_zero_data(&data);
    expect_zero_metadata(&metadata);
}

static void test_all_six_valid_exc_return_forms(void)
{
    static const uint32_t forms[] = {
        OAMR_EXC_RETURN_HANDLER_MSP_BASIC,
        OAMR_EXC_RETURN_THREAD_MSP_BASIC,
        OAMR_EXC_RETURN_THREAD_PSP_BASIC,
        OAMR_EXC_RETURN_HANDLER_MSP_EXTENDED,
        OAMR_EXC_RETURN_THREAD_MSP_EXTENDED,
        OAMR_EXC_RETURN_THREAD_PSP_EXTENDED
    };
    size_t form_index;

    CHECK(sizeof(oamr_exception_capture_metadata_t) == 12u);
    CHECK(OAMR_EXCEPTION_BASIC_LR_INDEX == 5u);
    CHECK(OAMR_EXCEPTION_BASIC_PC_INDEX == 6u);
    CHECK(OAMR_EXCEPTION_BASIC_XPSR_INDEX == 7u);
    CHECK(OAMR_EXCEPTION_EXTENDED_LR_INDEX == 23u);
    CHECK(OAMR_EXCEPTION_EXTENDED_PC_INDEX == 24u);
    CHECK(OAMR_EXCEPTION_EXTENDED_XPSR_INDEX == 25u);
    CHECK(oamr_exception_frame_required_words(0u) == 0u);

    for (form_index = 0u; form_index < sizeof(forms) / sizeof(forms[0]);
         ++form_index) {
        _Alignas(8) uint32_t words[OAMR_EXCEPTION_EXTENDED_FRAME_WORDS];
        oamr_exception_scb_snapshot_t scb = sample_scb();
        oamr_fault_record_data_t data = {0};
        oamr_exception_capture_metadata_t metadata = {0};
        uint32_t exc_return = forms[form_index];
        size_t count = oamr_exception_frame_required_words(exc_return);
        uint16_t expected_flags = OAMR_EXCEPTION_CAPTURE_STACKED_VALID;
        bool extended = count == OAMR_EXCEPTION_EXTENDED_FRAME_WORDS;
        size_t lr_index = extended ? OAMR_EXCEPTION_EXTENDED_LR_INDEX :
                          OAMR_EXCEPTION_BASIC_LR_INDEX;
        size_t pc_index = extended ? OAMR_EXCEPTION_EXTENDED_PC_INDEX :
                          OAMR_EXCEPTION_BASIC_PC_INDEX;
        size_t xpsr_index = extended ? OAMR_EXCEPTION_EXTENDED_XPSR_INDEX :
                            OAMR_EXCEPTION_BASIC_XPSR_INDEX;

        fill_frame(words, count);
        if (extended) {
            expected_flags |= OAMR_EXCEPTION_CAPTURE_EXTENDED_FRAME;
        }
        if ((exc_return & UINT32_C(0x00000004)) != 0u) {
            expected_flags |= OAMR_EXCEPTION_CAPTURE_USES_PSP;
        }
        CHECK(oamr_exception_frame_decode(words, count, exc_return, TEST_KIND,
                                          &scb, TEST_RESET_FLAGS, &data,
                                          &metadata) == OAMR_STATUS_OK);
        CHECK(data.stacked_lr == words[lr_index]);
        CHECK(data.stacked_pc == words[pc_index]);
        CHECK(data.stacked_xpsr == words[xpsr_index]);
        expect_raw_snapshot(&data, &scb, TEST_RESET_FLAGS);
        CHECK(metadata.sequence == 0u);
        CHECK(metadata.exc_return == exc_return);
        CHECK(metadata.exception_kind == (uint16_t)TEST_KIND);
        CHECK(metadata.flags == expected_flags);
    }
}

static void test_extended_layout_uses_eighteen_word_fp_prefix(void)
{
    _Alignas(8) uint32_t words[OAMR_EXCEPTION_EXTENDED_FRAME_WORDS];
    oamr_exception_scb_snapshot_t scb = sample_scb();
    oamr_fault_record_data_t data = {0};
    oamr_exception_capture_metadata_t metadata = {0};

    fill_frame(words, OAMR_EXCEPTION_EXTENDED_FRAME_WORDS);
    CHECK(oamr_exception_frame_decode(words, OAMR_EXCEPTION_EXTENDED_FRAME_WORDS,
                                      OAMR_EXC_RETURN_THREAD_PSP_EXTENDED,
                                      OAMR_EXCEPTION_KIND_HARD_FAULT, &scb,
                                      TEST_RESET_FLAGS, &data, &metadata) ==
          OAMR_STATUS_OK);
    CHECK(data.stacked_lr == UINT32_C(0x51001717));
    CHECK(data.stacked_pc == UINT32_C(0x51001818));
    CHECK(data.stacked_xpsr == UINT32_C(0x51001919));
    CHECK(data.stacked_lr != words[OAMR_EXCEPTION_BASIC_LR_INDEX]);
    CHECK(data.stacked_pc != words[OAMR_EXCEPTION_BASIC_PC_INDEX]);
    CHECK(data.stacked_xpsr != words[OAMR_EXCEPTION_BASIC_XPSR_INDEX]);
}

static void test_reserved_and_invalid_exc_return_forms(void)
{
    static const uint32_t invalid_forms[] = {
        UINT32_C(0x00000000), UINT32_C(0xffffffff),
        UINT32_C(0xfffffff0), UINT32_C(0xfffffff8),
        UINT32_C(0xfffffffc), UINT32_C(0xfffffffe),
        UINT32_C(0xffffffe0), UINT32_C(0xffffffe8),
        UINT32_C(0xffffffec), UINT32_C(0xffffffee),
        UINT32_C(0xffffb1f1), UINT32_C(0xffffffb1),
        UINT32_C(0xfffffff3), UINT32_C(0xffffffd1)
    };
    uint32_t one_word = UINT32_C(0xdeadbeef);
    size_t index;

    /* One available word makes an accidental basic or extended read fail under
     * ASan, rather than merely checking a returned error code. */
    for (index = 0u; index < sizeof(invalid_forms) / sizeof(invalid_forms[0]);
         ++index) {
        CHECK(oamr_exception_frame_required_words(invalid_forms[index]) == 0u);
        expect_invalid_decode(&one_word, 1u, invalid_forms[index]);
    }
}

static void test_null_truncated_and_misaligned_frames(void)
{
    _Alignas(8) uint32_t basic[OAMR_EXCEPTION_BASIC_FRAME_WORDS];
    _Alignas(8) uint32_t extended[OAMR_EXCEPTION_EXTENDED_FRAME_WORDS];
    _Alignas(8) uint8_t unaligned_storage[sizeof(uint32_t) * 8u];
    const uint32_t *misaligned = (const uint32_t *)(const void *)(unaligned_storage + 1u);
    size_t count;

    fill_frame(basic, OAMR_EXCEPTION_BASIC_FRAME_WORDS);
    fill_frame(extended, OAMR_EXCEPTION_EXTENDED_FRAME_WORDS);
    memset(unaligned_storage, 0x5a, sizeof(unaligned_storage));

    expect_invalid_decode(NULL, OAMR_EXCEPTION_EXTENDED_FRAME_WORDS,
                          OAMR_EXC_RETURN_THREAD_MSP_BASIC);
    for (count = 0u; count < OAMR_EXCEPTION_BASIC_FRAME_WORDS; ++count) {
        expect_invalid_decode(basic, count, OAMR_EXC_RETURN_HANDLER_MSP_BASIC);
    }
    for (count = 0u; count < OAMR_EXCEPTION_EXTENDED_FRAME_WORDS; ++count) {
        expect_invalid_decode(extended, count,
                              OAMR_EXC_RETURN_THREAD_PSP_EXTENDED);
    }
    expect_invalid_decode(misaligned, OAMR_EXCEPTION_BASIC_FRAME_WORDS,
                          OAMR_EXC_RETURN_HANDLER_MSP_BASIC);
}

static void test_invalid_arguments_zero_outputs(void)
{
    _Alignas(8) uint32_t words[OAMR_EXCEPTION_BASIC_FRAME_WORDS];
    oamr_exception_scb_snapshot_t scb = sample_scb();
    oamr_fault_record_data_t data;
    oamr_exception_capture_metadata_t metadata;

    fill_frame(words, OAMR_EXCEPTION_BASIC_FRAME_WORDS);
    memset(&data, 0xa5, sizeof(data));
    memset(&metadata, 0, sizeof(metadata));
    CHECK(oamr_exception_frame_decode(words, OAMR_EXCEPTION_BASIC_FRAME_WORDS,
                                      OAMR_EXC_RETURN_THREAD_MSP_BASIC, TEST_KIND,
                                      &scb, TEST_RESET_FLAGS, NULL, &metadata) ==
          OAMR_STATUS_INVALID);
    expect_zero_metadata(&metadata);

    memset(&metadata, 0xa5, sizeof(metadata));
    memset(&data, 0, sizeof(data));
    CHECK(oamr_exception_frame_decode(words, OAMR_EXCEPTION_BASIC_FRAME_WORDS,
                                      OAMR_EXC_RETURN_THREAD_MSP_BASIC, TEST_KIND,
                                      &scb, TEST_RESET_FLAGS, &data, NULL) ==
          OAMR_STATUS_INVALID);
    expect_zero_data(&data);

    memset(&data, 0xa5, sizeof(data));
    memset(&metadata, 0xa5, sizeof(metadata));
    CHECK(oamr_exception_frame_decode(words, OAMR_EXCEPTION_BASIC_FRAME_WORDS,
                                      OAMR_EXC_RETURN_THREAD_MSP_BASIC, TEST_KIND,
                                      NULL, TEST_RESET_FLAGS, &data, &metadata) ==
          OAMR_STATUS_INVALID);
    expect_zero_data(&data);
    expect_zero_metadata(&metadata);

    memset(&data, 0xa5, sizeof(data));
    memset(&metadata, 0xa5, sizeof(metadata));
    CHECK(oamr_exception_frame_decode(words, OAMR_EXCEPTION_BASIC_FRAME_WORDS,
                                      OAMR_EXC_RETURN_THREAD_MSP_BASIC,
                                      (uint32_t)OAMR_EXCEPTION_KIND_USAGE_FAULT + 1u,
                                      &scb, TEST_RESET_FLAGS, &data, &metadata) ==
          OAMR_STATUS_INVALID);
    expect_zero_data(&data);
    expect_zero_metadata(&metadata);
}

static void test_stacking_errors_suppress_all_stacked_values(void)
{
    static const uint32_t errors[] = {
        OAMR_EXCEPTION_CFSR_STKERR,
        OAMR_EXCEPTION_CFSR_MSTKERR,
        OAMR_EXCEPTION_CFSR_STKERR | OAMR_EXCEPTION_CFSR_MSTKERR
    };
    static const uint32_t forms[] = {
        OAMR_EXC_RETURN_THREAD_PSP_BASIC,
        OAMR_EXC_RETURN_THREAD_PSP_EXTENDED
    };
    size_t error_index;
    size_t form_index;

    for (error_index = 0u; error_index < sizeof(errors) / sizeof(errors[0]);
         ++error_index) {
        for (form_index = 0u; form_index < sizeof(forms) / sizeof(forms[0]);
             ++form_index) {
            _Alignas(8) uint32_t words[OAMR_EXCEPTION_EXTENDED_FRAME_WORDS];
            oamr_exception_scb_snapshot_t scb = sample_scb();
            oamr_fault_record_data_t data = {0};
            oamr_exception_capture_metadata_t metadata = {0};
            uint32_t exc_return = forms[form_index];
            size_t count = oamr_exception_frame_required_words(exc_return);
            uint16_t expected_flags = OAMR_EXCEPTION_CAPTURE_USES_PSP;

            scb.cfsr |= errors[error_index];
            scb.cfsr |= OAMR_EXCEPTION_CFSR_BFARVALID |
                        OAMR_EXCEPTION_CFSR_MMARVALID;
            fill_frame(words, count);
            if (count == OAMR_EXCEPTION_EXTENDED_FRAME_WORDS) {
                expected_flags |= OAMR_EXCEPTION_CAPTURE_EXTENDED_FRAME;
            }
            expected_flags |= OAMR_EXCEPTION_CAPTURE_BFAR_VALID |
                              OAMR_EXCEPTION_CAPTURE_MMFAR_VALID;
            CHECK(oamr_exception_frame_decode(words, count, exc_return, TEST_KIND,
                                              &scb, TEST_RESET_FLAGS, &data,
                                              &metadata) == OAMR_STATUS_OK);
            CHECK(data.stacked_pc == 0u);
            CHECK(data.stacked_lr == 0u);
            CHECK(data.stacked_xpsr == 0u);
            expect_raw_snapshot(&data, &scb, TEST_RESET_FLAGS);
            CHECK(metadata.sequence == 0u);
            CHECK(metadata.exc_return == exc_return);
            CHECK(metadata.exception_kind == (uint16_t)TEST_KIND);
            CHECK((metadata.flags & OAMR_EXCEPTION_CAPTURE_STACKED_VALID) == 0u);
            CHECK(metadata.flags == expected_flags);
        }
    }
}

static void test_bfar_and_mmfar_validity_is_independent_of_value(void)
{
    static const struct {
        uint32_t valid_bits;
        uint32_t bfar;
        uint32_t mmar;
        uint16_t expected_flags;
    } cases[] = {
        {0u, UINT32_C(0xdeadbeef), UINT32_C(0xcafebabe), 0u},
        {OAMR_EXCEPTION_CFSR_BFARVALID, 0u, UINT32_C(0xcafebabe),
         OAMR_EXCEPTION_CAPTURE_BFAR_VALID},
        {OAMR_EXCEPTION_CFSR_MMARVALID, UINT32_C(0xdeadbeef), 0u,
         OAMR_EXCEPTION_CAPTURE_MMFAR_VALID},
        {OAMR_EXCEPTION_CFSR_BFARVALID | OAMR_EXCEPTION_CFSR_MMARVALID, 0u, 0u,
         OAMR_EXCEPTION_CAPTURE_BFAR_VALID | OAMR_EXCEPTION_CAPTURE_MMFAR_VALID}
    };
    _Alignas(8) uint32_t words[OAMR_EXCEPTION_BASIC_FRAME_WORDS];
    size_t index;

    fill_frame(words, OAMR_EXCEPTION_BASIC_FRAME_WORDS);
    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        oamr_exception_scb_snapshot_t scb = sample_scb();
        oamr_fault_record_data_t data = {0};
        oamr_exception_capture_metadata_t metadata = {0};

        scb.cfsr = TEST_CFSR | cases[index].valid_bits;
        scb.bfar = cases[index].bfar;
        scb.mmar = cases[index].mmar;
        CHECK(oamr_exception_frame_decode(words, OAMR_EXCEPTION_BASIC_FRAME_WORDS,
                                          OAMR_EXC_RETURN_THREAD_MSP_BASIC,
                                          TEST_KIND, &scb, TEST_RESET_FLAGS,
                                          &data, &metadata) == OAMR_STATUS_OK);
        expect_raw_snapshot(&data, &scb, TEST_RESET_FLAGS);
        CHECK(data.stacked_pc == words[OAMR_EXCEPTION_BASIC_PC_INDEX]);
        CHECK((metadata.flags & OAMR_EXCEPTION_CAPTURE_STACKED_VALID) != 0u);
        CHECK(metadata.flags ==
              (uint16_t)(OAMR_EXCEPTION_CAPTURE_STACKED_VALID |
                         cases[index].expected_flags));
    }
}

static void test_scb_only_helper_and_kind_range(void)
{
    oamr_exception_scb_snapshot_t scb = sample_scb();
    uint32_t kind;

    scb.cfsr |= OAMR_EXCEPTION_CFSR_BFARVALID;
    for (kind = (uint32_t)OAMR_EXCEPTION_KIND_NONE;
         kind <= (uint32_t)OAMR_EXCEPTION_KIND_USAGE_FAULT; ++kind) {
        oamr_fault_record_data_t data = {0};
        oamr_exception_capture_metadata_t metadata = {0};

        CHECK(oamr_exception_capture_scb_only(kind, &scb, TEST_RESET_FLAGS,
                                              &data, &metadata) == OAMR_STATUS_OK);
        CHECK(data.stacked_pc == 0u);
        CHECK(data.stacked_lr == 0u);
        CHECK(data.stacked_xpsr == 0u);
        expect_raw_snapshot(&data, &scb, TEST_RESET_FLAGS);
        CHECK(metadata.sequence == 0u);
        CHECK(metadata.exc_return == 0u);
        CHECK(metadata.exception_kind == (uint16_t)kind);
        CHECK(metadata.flags == OAMR_EXCEPTION_CAPTURE_BFAR_VALID);
    }

    {
        oamr_fault_record_data_t data;
        oamr_exception_capture_metadata_t metadata;

        memset(&data, 0xa5, sizeof(data));
        memset(&metadata, 0xa5, sizeof(metadata));
        CHECK(oamr_exception_capture_scb_only(
                  (uint32_t)OAMR_EXCEPTION_KIND_USAGE_FAULT + 1u, &scb,
                  TEST_RESET_FLAGS, &data, &metadata) == OAMR_STATUS_INVALID);
        expect_zero_data(&data);
        expect_zero_metadata(&metadata);

        memset(&data, 0xa5, sizeof(data));
        memset(&metadata, 0xa5, sizeof(metadata));
        CHECK(oamr_exception_capture_scb_only(TEST_KIND, NULL, TEST_RESET_FLAGS,
                                              &data, &metadata) ==
              OAMR_STATUS_INVALID);
        expect_zero_data(&data);
        expect_zero_metadata(&metadata);

        memset(&metadata, 0xa5, sizeof(metadata));
        CHECK(oamr_exception_capture_scb_only(TEST_KIND, &scb, TEST_RESET_FLAGS,
                                              NULL, &metadata) ==
              OAMR_STATUS_INVALID);
        expect_zero_metadata(&metadata);

        memset(&data, 0xa5, sizeof(data));
        CHECK(oamr_exception_capture_scb_only(TEST_KIND, &scb, TEST_RESET_FLAGS,
                                              &data, NULL) == OAMR_STATUS_INVALID);
        expect_zero_data(&data);
    }
}

static void test_input_and_output_guard_bytes(void)
{
    typedef struct {
        uint64_t before;
        uint32_t words[OAMR_EXCEPTION_EXTENDED_FRAME_WORDS];
        uint64_t after;
    } guarded_frame_t;
    typedef struct {
        uint64_t before;
        oamr_fault_record_data_t data;
        oamr_exception_capture_metadata_t metadata;
        uint64_t after;
    } guarded_outputs_t;
    _Alignas(8) guarded_frame_t input;
    _Alignas(8) guarded_outputs_t output;
    oamr_exception_scb_snapshot_t scb = sample_scb();
    size_t index;

    input.before = UINT64_C(0x1122334455667788);
    input.after = UINT64_C(0x8877665544332211);
    output.before = UINT64_C(0xa5a5a5a5a5a5a5a5);
    output.after = UINT64_C(0x5a5a5a5a5a5a5a5a);
    memset(&output.data, 0xa5, sizeof(output.data));
    memset(&output.metadata, 0xa5, sizeof(output.metadata));
    for (index = 0u; index < OAMR_EXCEPTION_EXTENDED_FRAME_WORDS; ++index) {
        input.words[index] = UINT32_C(0x71000000) + (uint32_t)index;
    }
    CHECK(oamr_exception_frame_decode(input.words,
                                      OAMR_EXCEPTION_EXTENDED_FRAME_WORDS,
                                      OAMR_EXC_RETURN_THREAD_PSP_EXTENDED,
                                      OAMR_EXCEPTION_KIND_USAGE_FAULT, &scb,
                                      UINT32_C(0xffffffff), &output.data,
                                      &output.metadata) == OAMR_STATUS_OK);
    CHECK(input.before == UINT64_C(0x1122334455667788));
    CHECK(input.after == UINT64_C(0x8877665544332211));
    CHECK(output.before == UINT64_C(0xa5a5a5a5a5a5a5a5));
    CHECK(output.after == UINT64_C(0x5a5a5a5a5a5a5a5a));
    CHECK(output.data.stacked_lr == input.words[23]);
    CHECK(output.data.stacked_pc == input.words[24]);
    CHECK(output.data.stacked_xpsr == input.words[25]);
    CHECK(output.metadata.sequence == 0u);
    CHECK(output.metadata.exc_return == OAMR_EXC_RETURN_THREAD_PSP_EXTENDED);
    CHECK(output.metadata.exception_kind == OAMR_EXCEPTION_KIND_USAGE_FAULT);
    CHECK(output.metadata.flags ==
          (OAMR_EXCEPTION_CAPTURE_STACKED_VALID |
           OAMR_EXCEPTION_CAPTURE_EXTENDED_FRAME |
           OAMR_EXCEPTION_CAPTURE_USES_PSP));
}

int main(void)
{
    RUN(test_all_six_valid_exc_return_forms);
    RUN(test_extended_layout_uses_eighteen_word_fp_prefix);
    RUN(test_reserved_and_invalid_exc_return_forms);
    RUN(test_null_truncated_and_misaligned_frames);
    RUN(test_invalid_arguments_zero_outputs);
    RUN(test_stacking_errors_suppress_all_stacked_values);
    RUN(test_bfar_and_mmfar_validity_is_independent_of_value);
    RUN(test_scb_only_helper_and_kind_range);
    RUN(test_input_and_output_guard_bytes);
    return TEST_END();
}
