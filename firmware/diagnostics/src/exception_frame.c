#include "oamr/exception_frame.h"

#include <stdbool.h>
#include <string.h>

#define OAMR_EXCEPTION_SP_ALIGNMENT ((size_t)8u)

static bool oamr_exception_frame_is_misaligned(const uint32_t *frame)
{
    return (((size_t)(uintptr_t)frame) % OAMR_EXCEPTION_SP_ALIGNMENT) != 0u;
}

static bool oamr_exception_kind_is_valid(uint32_t exception_kind)
{
    return exception_kind <= (uint32_t)OAMR_EXCEPTION_KIND_USAGE_FAULT;
}

static void oamr_exception_zero_outputs(
    oamr_fault_record_data_t *out_data,
    oamr_exception_capture_metadata_t *out_metadata)
{
    if (out_data != NULL) {
        memset(out_data, 0, sizeof(*out_data));
    }
    if (out_metadata != NULL) {
        memset(out_metadata, 0, sizeof(*out_metadata));
    }
}

size_t oamr_exception_frame_required_words(uint32_t exc_return)
{
    switch (exc_return) {
    case OAMR_EXC_RETURN_HANDLER_MSP_BASIC:
    case OAMR_EXC_RETURN_THREAD_MSP_BASIC:
    case OAMR_EXC_RETURN_THREAD_PSP_BASIC:
        return OAMR_EXCEPTION_BASIC_FRAME_WORDS;
    case OAMR_EXC_RETURN_HANDLER_MSP_EXTENDED:
    case OAMR_EXC_RETURN_THREAD_MSP_EXTENDED:
    case OAMR_EXC_RETURN_THREAD_PSP_EXTENDED:
        return OAMR_EXCEPTION_EXTENDED_FRAME_WORDS;
    default:
        return 0u;
    }
}

oamr_status_t oamr_exception_capture_scb_only(
    uint32_t exception_kind,
    const oamr_exception_scb_snapshot_t *scb,
    uint32_t raw_reset_flags,
    oamr_fault_record_data_t *out_data,
    oamr_exception_capture_metadata_t *out_metadata)
{
    if (!oamr_exception_kind_is_valid(exception_kind) || scb == NULL ||
        out_data == NULL || out_metadata == NULL) {
        oamr_exception_zero_outputs(out_data, out_metadata);
        return OAMR_STATUS_INVALID;
    }

    memset(out_data, 0, sizeof(*out_data));
    memset(out_metadata, 0, sizeof(*out_metadata));
    out_data->raw_reset_flags = raw_reset_flags;
    out_data->cfsr = scb->cfsr;
    out_data->hfsr = scb->hfsr;
    out_data->bfar = scb->bfar;
    out_data->mmar = scb->mmar;
    out_metadata->exception_kind = (uint16_t)exception_kind;
    if ((scb->cfsr & OAMR_EXCEPTION_CFSR_BFARVALID) != 0u) {
        out_metadata->flags |= (uint16_t)OAMR_EXCEPTION_CAPTURE_BFAR_VALID;
    }
    if ((scb->cfsr & OAMR_EXCEPTION_CFSR_MMARVALID) != 0u) {
        out_metadata->flags |= (uint16_t)OAMR_EXCEPTION_CAPTURE_MMFAR_VALID;
    }
    return OAMR_STATUS_OK;
}

oamr_status_t oamr_exception_frame_decode(
    const uint32_t *frame,
    size_t frame_word_count,
    uint32_t exc_return,
    uint32_t exception_kind,
    const oamr_exception_scb_snapshot_t *scb,
    uint32_t raw_reset_flags,
    oamr_fault_record_data_t *out_data,
    oamr_exception_capture_metadata_t *out_metadata)
{
    size_t required_words = oamr_exception_frame_required_words(exc_return);
    oamr_status_t status;

    if (required_words == 0u || frame == NULL ||
        oamr_exception_frame_is_misaligned(frame) ||
        frame_word_count < required_words) {
        oamr_exception_zero_outputs(out_data, out_metadata);
        return OAMR_STATUS_INVALID;
    }

    status = oamr_exception_capture_scb_only(
        exception_kind, scb, raw_reset_flags, out_data, out_metadata);
    if (status != OAMR_STATUS_OK) {
        return status;
    }

    out_metadata->exc_return = exc_return;
    if ((exc_return & UINT32_C(0x00000004)) != 0u) {
        out_metadata->flags |= (uint16_t)OAMR_EXCEPTION_CAPTURE_USES_PSP;
    }
    if (required_words == OAMR_EXCEPTION_EXTENDED_FRAME_WORDS) {
        out_metadata->flags |= (uint16_t)OAMR_EXCEPTION_CAPTURE_EXTENDED_FRAME;
    }

    /* A stacking failure makes the claimed frame contents unusable, including
     * a frame whose extended layout was selected by EXC_RETURN bit 4. */
    if ((scb->cfsr &
         (OAMR_EXCEPTION_CFSR_STKERR | OAMR_EXCEPTION_CFSR_MSTKERR)) != 0u) {
        return OAMR_STATUS_OK;
    }

    if (required_words == OAMR_EXCEPTION_EXTENDED_FRAME_WORDS) {
        out_data->stacked_lr = frame[OAMR_EXCEPTION_EXTENDED_LR_INDEX];
        out_data->stacked_pc = frame[OAMR_EXCEPTION_EXTENDED_PC_INDEX];
        out_data->stacked_xpsr = frame[OAMR_EXCEPTION_EXTENDED_XPSR_INDEX];
    } else {
        out_data->stacked_lr = frame[OAMR_EXCEPTION_BASIC_LR_INDEX];
        out_data->stacked_pc = frame[OAMR_EXCEPTION_BASIC_PC_INDEX];
        out_data->stacked_xpsr = frame[OAMR_EXCEPTION_BASIC_XPSR_INDEX];
    }
    out_metadata->flags |= (uint16_t)OAMR_EXCEPTION_CAPTURE_STACKED_VALID;
    return OAMR_STATUS_OK;
}
