#ifndef FAULT_CAPTURE_H
#define FAULT_CAPTURE_H

#include "main.h"

#include "oamr/exception_frame.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Assembly veneers supply the original EXC_RETURN and selected stack pointer.
 * The backend only observes registers and writes bounded retained diagnostics.
 * It never transmits, allocates, recovers, or changes actuator/safety state. */
void fault_capture_from_exception(const uint32_t *stack_pointer,
                                  uint32_t exc_return,
                                  uint32_t exception_kind);

/* Normal-context ownership of copied boot evidence and explicit maintenance
 * acknowledgement. Reporting and UART output do not clear retained evidence. */
void fault_capture_boot_init(void);
void fault_capture_boot_complete(void);
bool fault_capture_storage_ready(void);
bool fault_capture_boot_has_record(void);
bool fault_capture_boot_get_snapshot(oamr_fault_record_t *out_record,
                                     oamr_exception_capture_metadata_t *out_metadata);
bool fault_capture_acknowledge(
    const oamr_fault_record_t *record,
    const oamr_exception_capture_metadata_t *metadata);

#if defined(OAMR_FAULT_CAPTURE_TEST_TERMINAL)
void oamr_fault_capture_test_terminal(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* FAULT_CAPTURE_H */
