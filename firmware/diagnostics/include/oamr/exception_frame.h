#ifndef OAMR_EXCEPTION_FRAME_H
#define OAMR_EXCEPTION_FRAME_H

#include "oamr/fault_record.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Cortex-M7 basic and extended (floating-point) exception-frame layouts. */
#define OAMR_EXCEPTION_BASIC_FRAME_WORDS ((size_t)8u)
#define OAMR_EXCEPTION_EXTENDED_FRAME_WORDS ((size_t)26u)
#define OAMR_EXCEPTION_EXTENDED_FP_WORDS ((size_t)18u)
#define OAMR_EXCEPTION_BASIC_LR_INDEX ((size_t)5u)
#define OAMR_EXCEPTION_BASIC_PC_INDEX ((size_t)6u)
#define OAMR_EXCEPTION_BASIC_XPSR_INDEX ((size_t)7u)
#define OAMR_EXCEPTION_EXTENDED_LR_INDEX ((size_t)23u)
#define OAMR_EXCEPTION_EXTENDED_PC_INDEX ((size_t)24u)
#define OAMR_EXCEPTION_EXTENDED_XPSR_INDEX ((size_t)25u)

/* The only valid EXC_RETURN forms on this non-TrustZone Cortex-M7 target. */
#define OAMR_EXC_RETURN_HANDLER_MSP_BASIC UINT32_C(0xfffffff1)
#define OAMR_EXC_RETURN_THREAD_MSP_BASIC UINT32_C(0xfffffff9)
#define OAMR_EXC_RETURN_THREAD_PSP_BASIC UINT32_C(0xfffffffd)
#define OAMR_EXC_RETURN_HANDLER_MSP_EXTENDED UINT32_C(0xffffffe1)
#define OAMR_EXC_RETURN_THREAD_MSP_EXTENDED UINT32_C(0xffffffe9)
#define OAMR_EXC_RETURN_THREAD_PSP_EXTENDED UINT32_C(0xffffffed)

/* CFSR bits needed to qualify the captured register values. */
#define OAMR_EXCEPTION_CFSR_MSTKERR UINT32_C(0x00000010)
#define OAMR_EXCEPTION_CFSR_MMARVALID UINT32_C(0x00000080)
#define OAMR_EXCEPTION_CFSR_STKERR UINT32_C(0x00001000)
#define OAMR_EXCEPTION_CFSR_BFARVALID UINT32_C(0x00008000)

/* Shared exception kinds; no frame-access outcome is inferred from the kind. */
typedef enum {
    OAMR_EXCEPTION_KIND_NONE = 0,
    OAMR_EXCEPTION_KIND_HARD_FAULT = 1,
    OAMR_EXCEPTION_KIND_MEM_MANAGE = 2,
    OAMR_EXCEPTION_KIND_BUS_FAULT = 3,
    OAMR_EXCEPTION_KIND_USAGE_FAULT = 4
} oamr_exception_kind_t;

typedef enum {
    OAMR_EXCEPTION_CAPTURE_STACKED_VALID = (1u << 0),
    OAMR_EXCEPTION_CAPTURE_EXTENDED_FRAME = (1u << 1),
    OAMR_EXCEPTION_CAPTURE_USES_PSP = (1u << 2),
    OAMR_EXCEPTION_CAPTURE_BFAR_VALID = (1u << 3),
    OAMR_EXCEPTION_CAPTURE_MMFAR_VALID = (1u << 4)
} oamr_exception_capture_flags_t;

/* Shared fixed-width metadata; retention, not this decoder, assigns sequence. */
typedef struct {
    uint32_t sequence;
    uint32_t exc_return;
    uint16_t exception_kind;
    uint16_t flags;
} oamr_exception_capture_metadata_t;

typedef struct {
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t bfar;
    uint32_t mmar;
} oamr_exception_scb_snapshot_t;

/* Return 8 or 26 for a valid form, or zero for a reserved/invalid form. */
size_t oamr_exception_frame_required_words(uint32_t exc_return);

/*
 * Decode one hardware-stacked basic or extended frame into schema-v1 data.
 *
 * frame_word_count must describe words actually readable at frame, and frame
 * must be 8-byte aligned. This function checks those inputs before reading any
 * frame word; it cannot establish that the claimed memory is mapped or that a
 * stack pointer identifies a genuine hardware frame. Reserved EXC_RETURN forms
 * are rejected without frame access. Outputs are zeroed on INVALID.
 *
 * CFSR STKERR or MSTKERR makes all stacked fields unavailable: the function
 * then performs no frame read, leaves those fields zero, clears STACKED_VALID,
 * and returns OK with the SCB snapshot intact.
 * BFAR/MMFAR validity is represented only by their independent CFSR flags.
 * No status register is cleared and no recovery or terminal action is taken.
 */
oamr_status_t oamr_exception_frame_decode(
    const uint32_t *frame,
    size_t frame_word_count,
    uint32_t exc_return,
    uint32_t exception_kind,
    const oamr_exception_scb_snapshot_t *scb,
    uint32_t raw_reset_flags,
    oamr_fault_record_data_t *out_data,
    oamr_exception_capture_metadata_t *out_metadata);

/*
 * Board-callable fallback when no trustworthy frame pointer/EXC_RETURN exists.
 * Stacked fields and exc_return are zero, and STACKED_VALID is clear. Raw
 * reset/SCB values and independent address-valid flags are kept.
 */
oamr_status_t oamr_exception_capture_scb_only(
    uint32_t exception_kind,
    const oamr_exception_scb_snapshot_t *scb,
    uint32_t raw_reset_flags,
    oamr_fault_record_data_t *out_data,
    oamr_exception_capture_metadata_t *out_metadata);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_EXCEPTION_FRAME_H */
