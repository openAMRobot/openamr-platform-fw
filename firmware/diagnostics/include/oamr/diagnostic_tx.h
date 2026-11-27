#ifndef OAMR_DIAGNOSTIC_TX_H
#define OAMR_DIAGNOSTIC_TX_H

#include "oamr/fault_record.h"
#include "oamr/reset_cause.h"
#include "oamr/status.h"
#include "oamr/transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OAMR_DIAGNOSTIC_FORMAT_CAPACITY ((size_t)512u)

/* Caller-provided observations; no field is discovered or inferred by TX. */
typedef struct {
    const char *image;
    const char *board;
    uint32_t boot_sequence;
} oamr_diagnostic_boot_t;

typedef struct {
    const char *firmware;
    const char *git_revision;
    const char *configuration;
} oamr_diagnostic_version_t;

typedef struct {
    uint32_t sysclk_hz;
    uint32_t hclk_hz;
    uint32_t pclk1_hz;
    uint32_t pclk2_hz;
} oamr_diagnostic_clock_t;

typedef struct {
    const char *name;
    oamr_status_t status;
    uint32_t detail;
} oamr_diagnostic_peripheral_t;

typedef struct {
    uint64_t enqueued_records;
    uint64_t enqueued_bytes;
    uint64_t transmitted_bytes;
    uint64_t would_block_calls;
    uint64_t would_block_bytes;
    uint64_t dropped_records;
    uint64_t dropped_bytes;
    uint64_t formatting_errors;
    uint64_t transport_errors;
    uint64_t error_bytes;
    size_t queued_bytes;
    size_t free_bytes;
    size_t capacity;
    bool initialized;
} oamr_diagnostic_tx_snapshot_t;

/*
 * Caller-owned bounded byte ring. Submission is atomic per complete record:
 * insufficient free capacity rejects the entire new record without
 * overwriting old data. No flush callback, retry, allocation or delay occurs
 * in submit. Public fields are implementation state, not caller configuration.
 * All operations need external serialization when shared with an ISR/thread.
 */
typedef struct {
    uint8_t *storage;
    size_t storage_size;
    size_t capacity;
    size_t head;
    size_t count;
    oamr_transport_t transport;
    uint64_t enqueued_records;
    uint64_t enqueued_bytes;
    uint64_t transmitted_bytes;
    uint64_t would_block_calls;
    uint64_t would_block_bytes;
    uint64_t dropped_records;
    uint64_t dropped_bytes;
    uint64_t formatting_errors;
    uint64_t transport_errors;
    uint64_t error_bytes;
    bool initialized;
} oamr_diagnostic_tx_t;

/* Bind storage and a nonblocking PR 3 transport write callback. */
oamr_status_t oamr_diagnostic_tx_init(oamr_diagnostic_tx_t *tx,
                                      void *storage,
                                      size_t storage_size,
                                      const oamr_transport_t *transport);

/* Copy one already formatted record; no implicit newline or data is added. */
oamr_status_t oamr_diagnostic_tx_submit(oamr_diagnostic_tx_t *tx,
                                        const void *data,
                                        size_t data_size);

/*
 * Make one bounded callback attempt. It may write only a prefix; remaining
 * bytes stay queued. OK includes zero-work when empty, and successful progress.
 * WOULD_BLOCK is returned and counted. A malformed OK transfer or a transport
 * failure is counted and returned; queued bytes are retained for an explicit
 * later policy decision, never falsely counted as delivered or capacity-dropped.
 */
oamr_transport_result_t oamr_diagnostic_tx_flush_once(oamr_diagnostic_tx_t *tx);

/* Format and atomically submit one caller-supplied report (including CRLF). */
oamr_status_t oamr_diagnostic_tx_report_boot(
    oamr_diagnostic_tx_t *tx,
    const oamr_diagnostic_boot_t *boot);
oamr_status_t oamr_diagnostic_tx_report_version(
    oamr_diagnostic_tx_t *tx,
    const oamr_diagnostic_version_t *version);
oamr_status_t oamr_diagnostic_tx_report_clock(
    oamr_diagnostic_tx_t *tx,
    const oamr_diagnostic_clock_t *clock);
oamr_status_t oamr_diagnostic_tx_report_peripheral(
    oamr_diagnostic_tx_t *tx,
    const oamr_diagnostic_peripheral_t *peripheral);
oamr_status_t oamr_diagnostic_tx_report_reset(
    oamr_diagnostic_tx_t *tx,
    const oamr_reset_cause_t *reset);
/* CRC and all fields are encoded from the supplied record, not generated. */
oamr_status_t oamr_diagnostic_tx_report_fault(
    oamr_diagnostic_tx_t *tx,
    const oamr_fault_record_t *fault);

oamr_status_t oamr_diagnostic_tx_get_snapshot(
    const oamr_diagnostic_tx_t *tx,
    oamr_diagnostic_tx_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_DIAGNOSTIC_TX_H */
