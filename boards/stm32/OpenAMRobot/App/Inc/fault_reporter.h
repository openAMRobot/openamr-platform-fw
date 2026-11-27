#ifndef FAULT_REPORTER_H
#define FAULT_REPORTER_H

#include "oamr/diagnostic_tx.h"
#include "oamr/exception_frame.h"
#include "oamr/fault_record.h"
#include "oamr/transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FAULT_REPORTER_STAGING_CAPACITY ((size_t)1024U)

typedef enum {
  FAULT_REPORTER_OK = 0,
  FAULT_REPORTER_INVALID = 1,
  FAULT_REPORTER_EMPTY = 2,
  FAULT_REPORTER_WOULD_BLOCK = 3,
  FAULT_REPORTER_ERROR = 4
} fault_reporter_result_t;

typedef struct {
  oamr_fault_record_t record;
  oamr_exception_capture_metadata_t metadata;
  uint8_t staging[FAULT_REPORTER_STAGING_CAPACITY];
  oamr_diagnostic_tx_t tx;
  oamr_transport_t transport;
  bool initialized;
  bool pending;
  bool metadata_enqueued;
  bool payload_enqueued;
} fault_reporter_t;

/* Bind a nonblocking whole-record transport. The reporter owns a private copy
 * of the supplied record and metadata; no retention storage is read or changed. */
bool fault_reporter_init(fault_reporter_t *reporter,
                         oamr_transport_write_callback_t write,
                         void *context);
/* Optional validation of a serialized retained snapshot before submission.
 * Reset definitions are supplied again, never restored from untrusted bytes. */
bool fault_reporter_submit_encoded(fault_reporter_t *reporter,
                                   const uint8_t *encoded_record,
                                   size_t encoded_size,
                                   const oamr_reset_flag_definitions_t *definitions,
                                   const oamr_exception_capture_metadata_t *metadata);
bool fault_reporter_submit(fault_reporter_t *reporter,
                           const oamr_fault_record_t *record,
                           const oamr_exception_capture_metadata_t *metadata);

/* Normal-context progress only. At most one new report stage is enqueued per
 * call, followed by at most one portable diagnostic_tx flush callback. A blocked
 * flush never causes an already accepted stage to be submitted again. */
fault_reporter_result_t fault_reporter_poll(fault_reporter_t *reporter);
bool fault_reporter_pending(const fault_reporter_t *reporter);
bool fault_reporter_enqueued(const fault_reporter_t *reporter);

#ifdef __cplusplus
}
#endif

#endif /* FAULT_REPORTER_H */
