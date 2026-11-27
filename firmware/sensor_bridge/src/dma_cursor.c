#include "oamr/dma_cursor.h"

#include <stddef.h>
#include <string.h>

static void increment(uint64_t *counter)
{
    if (*counter < UINT64_MAX) {
        ++*counter;
    }
}

static void discard(oamr_dma_cursor_t *cursor, uint64_t count)
{
    if (UINT64_MAX - cursor->counters.discarded_bytes < count) {
        cursor->counters.discarded_bytes = UINT64_MAX;
    } else {
        cursor->counters.discarded_bytes += count;
    }
}

static bool valid_cursor(const oamr_dma_cursor_t *cursor)
{
    return cursor != NULL && cursor->configured && cursor->buffer_size > 0u &&
           cursor->observed_position < cursor->buffer_size &&
           cursor->read_absolute <= cursor->write_absolute;
}

static oamr_status_t apply_producer_position(oamr_dma_cursor_t *cursor,
                                             uint64_t write_absolute)
{
    if (write_absolute < cursor->write_absolute) {
        increment(&cursor->counters.invalid_updates);
        return OAMR_STATUS_INVALID;
    }
    if (write_absolute == cursor->write_absolute) {
        return cursor->resync_required ? OAMR_STATUS_OVERFLOW : OAMR_STATUS_OK;
    }
    if (cursor->resync_required) {
        /* Only new producer progress remains after the latched loss event. */
        discard(cursor, write_absolute - cursor->write_absolute);
        cursor->write_absolute = write_absolute;
        cursor->read_absolute = write_absolute;
        return OAMR_STATUS_OVERFLOW;
    }

    if (write_absolute - cursor->write_absolute > (uint64_t)cursor->buffer_size) {
        increment(&cursor->counters.gaps);
    }
    cursor->write_absolute = write_absolute;
    if (write_absolute - cursor->read_absolute > (uint64_t)cursor->buffer_size) {
        discard(cursor, write_absolute - cursor->read_absolute);
        cursor->read_absolute = write_absolute;
        increment(&cursor->counters.overruns);
        cursor->resync_required = true;
        return OAMR_STATUS_OVERFLOW;
    }
    if (cursor->resync_required) {
        /* New bytes may arrive while latched, but no unflushed span escapes. */
        discard(cursor, write_absolute - cursor->read_absolute);
        cursor->read_absolute = write_absolute;
        return OAMR_STATUS_OVERFLOW;
    }
    return OAMR_STATUS_OK;
}

oamr_status_t oamr_dma_cursor_init(oamr_dma_cursor_t *cursor,
                                   size_t buffer_size,
                                   uint64_t initial_absolute_position)
{
    if (cursor == NULL) {
        return OAMR_STATUS_INVALID;
    }

    (void)memset(cursor, 0, sizeof(*cursor));
    if (buffer_size == 0u) {
        return OAMR_STATUS_INVALID;
    }

    cursor->buffer_size = buffer_size;
    cursor->observed_position =
        (size_t)(initial_absolute_position % (uint64_t)buffer_size);
    cursor->read_absolute = initial_absolute_position;
    cursor->write_absolute = initial_absolute_position;
    cursor->configured = true;
    return OAMR_STATUS_OK;
}

oamr_status_t oamr_dma_cursor_update_ndtr(oamr_dma_cursor_t *cursor,
                                          size_t ndtr)
{
    size_t position;
    size_t delta;

    if (!valid_cursor(cursor) || ndtr > cursor->buffer_size) {
        if (cursor != NULL) {
            increment(&cursor->counters.invalid_updates);
        }
        return OAMR_STATUS_INVALID;
    }

    position = cursor->buffer_size - ndtr;
    if (position == cursor->buffer_size) {
        position = 0u;
    }

    if (position >= cursor->observed_position) {
        delta = position - cursor->observed_position;
    } else {
        delta = cursor->buffer_size - cursor->observed_position + position;
    }
    cursor->observed_position = position;

    if ((uint64_t)delta > UINT64_MAX - cursor->write_absolute) {
        increment(&cursor->counters.invalid_updates);
        return OAMR_STATUS_INVALID;
    }
    return apply_producer_position(cursor, cursor->write_absolute + (uint64_t)delta);
}

oamr_status_t oamr_dma_cursor_update_absolute(oamr_dma_cursor_t *cursor,
                                              uint64_t write_absolute)
{
    if (!valid_cursor(cursor)) {
        return OAMR_STATUS_INVALID;
    }
    return apply_producer_position(cursor, write_absolute);
}

size_t oamr_dma_cursor_available(const oamr_dma_cursor_t *cursor)
{
    uint64_t available;

    if (!valid_cursor(cursor) || cursor->resync_required) {
        return 0u;
    }
    available = cursor->write_absolute - cursor->read_absolute;
    return available > (uint64_t)SIZE_MAX ? SIZE_MAX : (size_t)available;
}

oamr_status_t oamr_dma_cursor_peek(const oamr_dma_cursor_t *cursor,
                                   oamr_dma_chunk_t *out)
{
    uint64_t available;

    if (out == NULL) {
        return OAMR_STATUS_INVALID;
    }
    out->offset = 0u;
    out->length = 0u;
    if (!valid_cursor(cursor)) {
        return OAMR_STATUS_INVALID;
    }
    if (cursor->resync_required) {
        return OAMR_STATUS_OVERFLOW;
    }

    available = cursor->write_absolute - cursor->read_absolute;
    if (available == 0u) {
        return OAMR_STATUS_EMPTY;
    }

    out->offset = (size_t)(cursor->read_absolute % (uint64_t)cursor->buffer_size);
    out->length = (size_t)available;
    if (out->length > cursor->buffer_size - out->offset) {
        out->length = cursor->buffer_size - out->offset;
    }
    return OAMR_STATUS_OK;
}

oamr_status_t oamr_dma_cursor_consume(oamr_dma_cursor_t *cursor,
                                      size_t length)
{
    uint64_t available;

    if (!valid_cursor(cursor) || cursor->resync_required) {
        return cursor != NULL && cursor->resync_required ? OAMR_STATUS_OVERFLOW
                                                          : OAMR_STATUS_INVALID;
    }
    available = cursor->write_absolute - cursor->read_absolute;
    if (length == 0u || (uint64_t)length > available) {
        return OAMR_STATUS_INVALID;
    }
    cursor->read_absolute += (uint64_t)length;
    return OAMR_STATUS_OK;
}

oamr_status_t oamr_dma_cursor_flush(oamr_dma_cursor_t *cursor)
{
    if (!valid_cursor(cursor)) {
        return OAMR_STATUS_INVALID;
    }

    discard(cursor, cursor->write_absolute - cursor->read_absolute);
    cursor->read_absolute = cursor->write_absolute;
    cursor->resync_required = false;
    return OAMR_STATUS_OK;
}
