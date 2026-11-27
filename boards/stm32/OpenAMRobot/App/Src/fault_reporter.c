#include "fault_reporter.h"

#include <stdio.h>
#include <string.h>

#define FAULT_REPORTER_METADATA_CAPACITY ((size_t)160U)

static bool fault_reporter_metadata_valid(
    const oamr_exception_capture_metadata_t *metadata)
{
  return metadata != NULL &&
         metadata->exception_kind != (uint16_t)OAMR_EXCEPTION_KIND_NONE &&
         metadata->exception_kind <= (uint16_t)OAMR_EXCEPTION_KIND_USAGE_FAULT &&
         (metadata->flags & (uint16_t)~(OAMR_EXCEPTION_CAPTURE_STACKED_VALID |
                                        OAMR_EXCEPTION_CAPTURE_EXTENDED_FRAME |
                                        OAMR_EXCEPTION_CAPTURE_USES_PSP |
                                        OAMR_EXCEPTION_CAPTURE_BFAR_VALID |
                                        OAMR_EXCEPTION_CAPTURE_MMFAR_VALID)) == 0U;
}

bool fault_reporter_init(fault_reporter_t *reporter,
                         const oamr_transport_write_callback_t write,
                         void *context)
{
  if (reporter == NULL)
  {
    return false;
  }

  memset(reporter, 0, sizeof(*reporter));
  if (write == NULL)
  {
    return false;
  }

  reporter->transport.context = context;
  reporter->transport.read = NULL;
  reporter->transport.write = write;
  if (oamr_diagnostic_tx_init(&reporter->tx, reporter->staging,
                              sizeof(reporter->staging), &reporter->transport) !=
      OAMR_STATUS_OK)
  {
    return false;
  }
  reporter->initialized = true;
  return true;
}

bool fault_reporter_submit(fault_reporter_t *reporter,
                           const oamr_fault_record_t *record,
                           const oamr_exception_capture_metadata_t *metadata)
{
  if (reporter == NULL || !reporter->initialized || record == NULL ||
      !fault_reporter_metadata_valid(metadata) || reporter->pending)
  {
    return false;
  }

  reporter->record = *record;
  reporter->metadata = *metadata;
  reporter->pending = true;
  reporter->metadata_enqueued = false;
  reporter->payload_enqueued = false;
  return true;
}

bool fault_reporter_submit_encoded(fault_reporter_t *reporter,
                                   const uint8_t *encoded_record,
                                   size_t encoded_size,
                                   const oamr_reset_flag_definitions_t *definitions,
                                   const oamr_exception_capture_metadata_t *metadata)
{
  oamr_fault_record_t decoded;

  if (reporter == NULL || !reporter->initialized || definitions == NULL ||
      oamr_fault_record_decode(encoded_record, encoded_size, &decoded) !=
          OAMR_STATUS_OK)
  {
    return false;
  }
  decoded.reset_definitions = *definitions;
  return fault_reporter_submit(reporter, &decoded, metadata);
}

static fault_reporter_result_t fault_reporter_enqueue_metadata(
    fault_reporter_t *reporter)
{
  char message[FAULT_REPORTER_METADATA_CAPACITY];
  int length;

  length = snprintf(message, sizeof(message),
                    "OAMR CAPTURE sequence=%lu,kind=%u,exc_return=0x%08lX,"
                    "flags=0x%04X\r\n",
                    (unsigned long)reporter->metadata.sequence,
                    (unsigned)reporter->metadata.exception_kind,
                    (unsigned long)reporter->metadata.exc_return,
                    (unsigned)reporter->metadata.flags);
  if (length <= 0 || (size_t)length >= sizeof(message))
  {
    return FAULT_REPORTER_ERROR;
  }
  if (oamr_diagnostic_tx_submit(&reporter->tx, message, (size_t)length) !=
      OAMR_STATUS_OK)
  {
    return FAULT_REPORTER_WOULD_BLOCK;
  }
  reporter->metadata_enqueued = true;
  return FAULT_REPORTER_OK;
}

fault_reporter_result_t fault_reporter_poll(fault_reporter_t *reporter)
{
  fault_reporter_result_t stage_result = FAULT_REPORTER_OK;
  oamr_transport_result_t flush_result;

  if (reporter == NULL || !reporter->initialized)
  {
    return FAULT_REPORTER_INVALID;
  }
  if (!reporter->pending)
  {
    return FAULT_REPORTER_EMPTY;
  }

  if (!reporter->metadata_enqueued || !reporter->payload_enqueued)
  {
    if (!reporter->metadata_enqueued)
    {
      stage_result = fault_reporter_enqueue_metadata(reporter);
    }
    else
    {
      /* This is the sole payload formatter: the real portable fault report. */
      oamr_status_t status = oamr_diagnostic_tx_report_fault(
          &reporter->tx, &reporter->record);
      if (status == OAMR_STATUS_OK)
      {
        reporter->payload_enqueued = true;
      }
      else
      {
        stage_result = status == OAMR_STATUS_OVERFLOW ?
                       FAULT_REPORTER_WOULD_BLOCK : FAULT_REPORTER_ERROR;
      }
    }
  }

  flush_result = oamr_diagnostic_tx_flush_once(&reporter->tx);
  if (reporter->payload_enqueued && reporter->tx.count == 0U)
  {
    /* Both complete records were accepted by the adapter. This is not a UART
     * physical-delivery acknowledgement and no retained record is cleared. */
    reporter->pending = false;
  }

  if (stage_result != FAULT_REPORTER_OK)
  {
    return stage_result;
  }
  if (flush_result == OAMR_TRANSPORT_WOULD_BLOCK)
  {
    return FAULT_REPORTER_WOULD_BLOCK;
  }
  if (flush_result != OAMR_TRANSPORT_OK)
  {
    return FAULT_REPORTER_ERROR;
  }
  return FAULT_REPORTER_OK;
}

bool fault_reporter_pending(const fault_reporter_t *reporter)
{
  return reporter != NULL && reporter->initialized && reporter->pending;
}

bool fault_reporter_enqueued(const fault_reporter_t *reporter)
{
  return fault_reporter_pending(reporter) && reporter->payload_enqueued;
}
