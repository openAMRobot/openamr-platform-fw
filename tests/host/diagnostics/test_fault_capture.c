/* SPDX-License-Identifier: MIT */
#include "test_harness.h"

#include "fault_capture.h"

#include "oamr/fault_retained.h"

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static RCC_TypeDef fake_rcc;
static SCB_TypeDef fake_scb;
static PWR_TypeDef fake_pwr;
RCC_TypeDef *RCC = &fake_rcc;
SCB_TypeDef *SCB = &fake_scb;
PWR_TypeDef *PWR = &fake_pwr;

uint32_t fault_test_bkpram_clock_calls;
uint32_t fault_test_backup_access_calls;
uint32_t fault_test_backup_disable_calls;
uint32_t fault_test_regulator_calls;
HAL_StatusTypeDef fault_test_regulator_status;
static unsigned terminal_calls;
static jmp_buf terminal_jump;
static bool terminal_armed;

extern oamr_fault_retained_storage_t retained_storage;

void HAL_PWR_EnableBkUpAccess(void)
{
    ++fault_test_backup_access_calls;
}

void HAL_PWR_DisableBkUpAccess(void)
{
    ++fault_test_backup_disable_calls;
}

HAL_StatusTypeDef HAL_PWREx_EnableBkUpReg(void)
{
    ++fault_test_regulator_calls;
    return fault_test_regulator_status;
}

/* Production fault_capture_from_exception() ends in an intentional infinite
 * loop. This hook is its only test seam and must not return: it unwinds to the
 * setjmp frame in trigger_exception(), which stands in for the non-returning
 * terminal state. An unarmed call is a harness error, not a pass.
 *
 * LIMITATION: this proves the terminal path is reached and that nothing
 * runs after the hook. It does NOT execute the production for(;;) loop;
 * longjmp replaces it. That the loop exists and is non-returning is
 * established only by source review (and could be checked on the ELF). */
void oamr_fault_capture_test_terminal(void)
{
    ++terminal_calls;
    if (!terminal_armed) {
        (void)fputs("test_fault_capture: terminal hook called while not armed\n",
                    stderr);
        abort();
    }
    terminal_armed = false;
    longjmp(terminal_jump, 1);
}

static void trigger_exception(const uint32_t *stack_pointer,
                              uint32_t exc_return,
                              uint32_t exception_kind)
{
    if (setjmp(terminal_jump) == 0) {
        terminal_armed = true;
        fault_capture_from_exception(stack_pointer, exc_return, exception_kind);
        /* Reached only if production returns instead of terminating. */
        terminal_armed = false;
        CHECK(!"fault_capture_from_exception returned without terminating");
    }
}

/* Reboot model: retained storage and the register fakes persist, normal boot
 * runs again and re-copies any committed record into the boot snapshot.
 * This models the fault_capture.h contract on the host. It is not proof of
 * physical reset behaviour or of retention through power loss, and an
 * acknowledgement here clears only the copied boot evidence, never a live fault
 * condition or any recovery state. */
static void simulate_reboot(void)
{
    fault_capture_boot_init();
}

static void reset_fixture(void)
{
    memset(&fake_rcc, 0, sizeof(fake_rcc));
    memset(&fake_scb, 0, sizeof(fake_scb));
    memset(&fake_pwr, 0, sizeof(fake_pwr));
    memset(&retained_storage, 0, sizeof(retained_storage));
    fault_test_bkpram_clock_calls = 0u;
    fault_test_backup_access_calls = 0u;
    fault_test_backup_disable_calls = 0u;
    fault_test_regulator_calls = 0u;
    fault_test_regulator_status = HAL_OK;
    terminal_calls = 0u;
    terminal_armed = false;
    fake_rcc.RSR = RCC_RSR_PORRSTF | RCC_RSR_IWDG1RSTF;
    fake_scb.CFSR = OAMR_EXCEPTION_CFSR_BFARVALID |
                    OAMR_EXCEPTION_CFSR_MMARVALID;
    fake_scb.HFSR = UINT32_C(0x40000000);
    fake_scb.BFAR = UINT32_C(0x12345678);
    fake_scb.MMFAR = UINT32_C(0x89abcdef);
}

static void boot_ready(void)
{
    reset_fixture();
    fake_pwr.CR2 = PWR_CR2_BRRDY;
    fault_capture_boot_init();
    CHECK(fault_capture_storage_ready());
}

static void test_boot_regulator_readiness_and_failure(void)
{
    boot_ready();
    CHECK(fault_test_bkpram_clock_calls == 1u);
    CHECK(fault_test_backup_access_calls == 1u);
    CHECK(fault_test_regulator_calls == 1u);
    CHECK(!fault_capture_boot_has_record());
    fault_capture_boot_complete();
    CHECK(fault_test_backup_disable_calls == 1u);

    reset_fixture();
    fault_capture_boot_init();
    CHECK(!fault_capture_storage_ready());
    CHECK(!fault_capture_boot_has_record());
    CHECK(fault_test_regulator_calls == 1u);

    reset_fixture();
    fault_test_regulator_status = HAL_ERROR;
    fault_capture_boot_init();
    CHECK(!fault_capture_storage_ready());
    CHECK(!fault_capture_boot_has_record());
}

static void test_exception_invalid_return_scb_only_and_next_boot(void)
{
    oamr_fault_record_t record;
    oamr_exception_capture_metadata_t metadata;

    boot_ready();
    trigger_exception(NULL, 0u, OAMR_EXCEPTION_KIND_HARD_FAULT);
    CHECK(terminal_calls == 1u);
    /* The boot copy is taken at boot only; a fault does not populate it. */
    CHECK(!fault_capture_boot_has_record());
    simulate_reboot();
    CHECK(fault_capture_boot_get_snapshot(&record, &metadata));
    CHECK(record.data.raw_reset_flags == fake_rcc.RSR);
    CHECK(record.data.cfsr == fake_scb.CFSR);
    CHECK(record.data.hfsr == fake_scb.HFSR);
    CHECK(record.data.bfar == fake_scb.BFAR);
    CHECK(record.data.mmar == fake_scb.MMFAR);
    CHECK(record.data.stacked_pc == 0u);
    CHECK(record.data.stacked_lr == 0u);
    CHECK(record.data.stacked_xpsr == 0u);
    CHECK(metadata.exception_kind == OAMR_EXCEPTION_KIND_HARD_FAULT);
    CHECK(metadata.exc_return == 0u);
    CHECK(metadata.sequence == 1u);
    CHECK((metadata.flags & OAMR_EXCEPTION_CAPTURE_STACKED_VALID) == 0u);
    CHECK((metadata.flags & OAMR_EXCEPTION_CAPTURE_BFAR_VALID) != 0u);
    CHECK((metadata.flags & OAMR_EXCEPTION_CAPTURE_MMFAR_VALID) != 0u);
}

static void test_exception_extended_frame_and_exact_ack(void)
{
    _Alignas(8) uint32_t frame[OAMR_EXCEPTION_EXTENDED_FRAME_WORDS] = {0};
    oamr_fault_record_t record;
    oamr_exception_capture_metadata_t metadata;
    oamr_exception_capture_metadata_t stale_metadata;

    boot_ready();
    frame[OAMR_EXCEPTION_EXTENDED_LR_INDEX] = UINT32_C(0x08000123);
    frame[OAMR_EXCEPTION_EXTENDED_PC_INDEX] = UINT32_C(0x08000457);
    frame[OAMR_EXCEPTION_EXTENDED_XPSR_INDEX] = UINT32_C(0x01000000);
    trigger_exception(frame, OAMR_EXC_RETURN_THREAD_PSP_EXTENDED,
                      OAMR_EXCEPTION_KIND_BUS_FAULT);
    simulate_reboot();
    CHECK(fault_capture_boot_get_snapshot(&record, &metadata));
    CHECK(record.data.stacked_lr == frame[OAMR_EXCEPTION_EXTENDED_LR_INDEX]);
    CHECK(record.data.stacked_pc == frame[OAMR_EXCEPTION_EXTENDED_PC_INDEX]);
    CHECK(record.data.stacked_xpsr == frame[OAMR_EXCEPTION_EXTENDED_XPSR_INDEX]);
    CHECK(metadata.exc_return == OAMR_EXC_RETURN_THREAD_PSP_EXTENDED);
    CHECK((metadata.flags & OAMR_EXCEPTION_CAPTURE_STACKED_VALID) != 0u);
    CHECK((metadata.flags & OAMR_EXCEPTION_CAPTURE_EXTENDED_FRAME) != 0u);
    CHECK((metadata.flags & OAMR_EXCEPTION_CAPTURE_USES_PSP) != 0u);

    stale_metadata = metadata;
    stale_metadata.sequence += 1u;
    CHECK(!fault_capture_acknowledge(&record, &stale_metadata));
    CHECK(fault_capture_boot_has_record());
    CHECK(!fault_capture_acknowledge(NULL, &metadata));
    CHECK(fault_capture_boot_has_record());
    CHECK(fault_capture_acknowledge(&record, &metadata));
    CHECK(!fault_capture_boot_has_record());
    CHECK(!fault_capture_acknowledge(&record, &metadata));
}

static void test_exception_storage_not_ready_never_calls_regulator(void)
{
    uint32_t frame[OAMR_EXCEPTION_BASIC_FRAME_WORDS] = {0};

    reset_fixture();
    fault_capture_boot_init();
    CHECK(!fault_capture_storage_ready());
    trigger_exception(frame, OAMR_EXC_RETURN_THREAD_MSP_BASIC,
                      OAMR_EXCEPTION_KIND_HARD_FAULT);
    CHECK(terminal_calls == 1u);
    CHECK(fault_test_regulator_calls == 1u); /* Boot only; exception adds none. */
    CHECK(fault_test_bkpram_clock_calls == 1u);
    CHECK(fault_test_backup_access_calls == 1u);
    CHECK(!fault_capture_boot_has_record());
}

static void test_boot_copy_survives_new_fault_and_stale_ack(void)
{
    _Alignas(8) uint32_t frame[OAMR_EXCEPTION_BASIC_FRAME_WORDS] = {0};
    oamr_fault_record_t record;
    oamr_exception_capture_metadata_t metadata;
    oamr_fault_record_t other;
    oamr_exception_capture_metadata_t other_metadata;

    boot_ready();
    frame[OAMR_EXCEPTION_BASIC_PC_INDEX] = UINT32_C(0x08001111);
    frame[OAMR_EXCEPTION_BASIC_LR_INDEX] = UINT32_C(0x08002222);
    frame[OAMR_EXCEPTION_BASIC_XPSR_INDEX] = UINT32_C(0x01000000);
    trigger_exception(frame, OAMR_EXC_RETURN_THREAD_MSP_BASIC,
                      OAMR_EXCEPTION_KIND_HARD_FAULT);
    simulate_reboot();
    CHECK(fault_capture_boot_get_snapshot(&record, &metadata));
    CHECK(record.data.stacked_pc == UINT32_C(0x08001111));
    CHECK(metadata.sequence == 1u);

    /* A new fault is retained but does not disturb the copied boot evidence. */
    frame[OAMR_EXCEPTION_BASIC_PC_INDEX] = UINT32_C(0x08003333);
    trigger_exception(frame, OAMR_EXC_RETURN_THREAD_MSP_BASIC,
                      OAMR_EXCEPTION_KIND_USAGE_FAULT);
    CHECK(fault_capture_boot_has_record());
    CHECK(fault_capture_boot_get_snapshot(&other, &other_metadata));
    CHECK(other.data.stacked_pc == UINT32_C(0x08001111));
    CHECK(other_metadata.sequence == 1u);

    /* Acknowledging the boot copy retires only that record and clears the copy.
     * The newer fault becomes visible at the next boot, not immediately. */
    CHECK(fault_capture_acknowledge(&record, &metadata));
    CHECK(!fault_capture_boot_has_record());
    simulate_reboot();
    CHECK(fault_capture_boot_get_snapshot(&record, &metadata));
    CHECK(record.data.stacked_pc == UINT32_C(0x08003333));
    CHECK(metadata.exception_kind == OAMR_EXCEPTION_KIND_USAGE_FAULT);
    CHECK(metadata.sequence == 2u);
}

int main(void)
{
    RUN(test_boot_regulator_readiness_and_failure);
    RUN(test_exception_invalid_return_scb_only_and_next_boot);
    RUN(test_exception_extended_frame_and_exact_ack);
    RUN(test_exception_storage_not_ready_never_calls_regulator);
    RUN(test_boot_copy_survives_new_fault_and_stale_ack);
    return TEST_END();
}
