#ifndef OAMR_DMA_CURSOR_H
#define OAMR_DMA_CURSOR_H

#include "oamr/status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Allocation-free circular-RX accounting. The caller owns the DMA buffer,
 * snapshots NDTR in application context, copies each returned span, and then
 * commits it with consume(). This module performs no IRQ processing, cache
 * maintenance, peripheral access, or buffer access.
 *
 * read_absolute and write_absolute identify bytes in a nondecreasing stream;
 * buffer offsets are absolute_position % buffer_size. update_ndtr() extends
 * the absolute producer position by the modular NDTR delta. As with every
 * NDTR-only cursor, an unobserved exact full wrap is indistinguishable from no
 * change: poll often enough, or use update_absolute() with a caller-extended
 * monotonic position. update_absolute() also detects producer jumps larger
 * than one buffer, which NDTR snapshots alone cannot detect.
 */
typedef struct {
    uint64_t overruns;
    uint64_t gaps;
    uint64_t discarded_bytes;
    uint64_t invalid_updates;
} oamr_dma_cursor_counters_t;

typedef struct {
    size_t offset;
    size_t length;
} oamr_dma_chunk_t;

typedef struct {
    size_t buffer_size;
    size_t observed_position;
    uint64_t read_absolute;
    uint64_t write_absolute;
    bool configured;
    bool resync_required;
    oamr_dma_cursor_counters_t counters;
} oamr_dma_cursor_t;

/* Establish a cursor at an explicit absolute stream position with no data. */
oamr_status_t oamr_dma_cursor_init(oamr_dma_cursor_t *cursor,
                                   size_t buffer_size,
                                   uint64_t initial_absolute_position);

/*
 * Accept NDTR in the inclusive range 0..buffer_size. Both 0 and buffer_size
 * denote wrapped write position zero. Invalid values change no positions.
 */
oamr_status_t oamr_dma_cursor_update_ndtr(oamr_dma_cursor_t *cursor,
                                          size_t ndtr);

/* A decreasing absolute position is INVALID and changes no positions. */
oamr_status_t oamr_dma_cursor_update_absolute(oamr_dma_cursor_t *cursor,
                                              uint64_t write_absolute);

/*
 * Return the first contiguous unread span. EMPTY means no bytes; OVERFLOW
 * means resynchronization is latched and no span may be consumed. On success,
 * out identifies at most buffer_size bytes and no position is advanced.
 */
oamr_status_t oamr_dma_cursor_peek(const oamr_dma_cursor_t *cursor,
                                   oamr_dma_chunk_t *out);

/* Commit exactly length previously peeked/copied bytes; no implicit consume. */
oamr_status_t oamr_dma_cursor_consume(oamr_dma_cursor_t *cursor,
                                      size_t length);

/* Number of currently consumable bytes, or zero if invalid/resync is needed. */
size_t oamr_dma_cursor_available(const oamr_dma_cursor_t *cursor);

/*
 * Resynchronization policy: a producer gap larger than buffer_size, or unread
 * data overwritten by producer progress, drops through the latest producer
 * position, returns OVERFLOW, and latches resync_required. No old/new fragment
 * is exposed while latched. After flushing the associated byte-stream parser,
 * call flush() to discard anything arrived since the event and clear the latch.
 * Explicit flush also discards ordinary unread bytes. Counters saturate.
 */
oamr_status_t oamr_dma_cursor_flush(oamr_dma_cursor_t *cursor);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_DMA_CURSOR_H */
