#ifndef OAMR_BOUNDED_QUEUE_H
#define OAMR_BOUNDED_QUEUE_H

#include "oamr/status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Single-producer/single-consumer storage is not implicitly synchronized.
 * Callers must serialize every operation if an ISR or another thread can
 * access the same queue concurrently.
 *
 * Elements are fixed-size, byte-copied objects. The queue owns no allocation
 * and never frees element/payload pointers. Element buffers passed to push,
 * pop, or peek must not overlap the queue storage.
 */
typedef struct {
    uint8_t *storage;
    size_t storage_size;
    size_t element_size;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    uint64_t overflow_count;
    bool initialized;
} oamr_bounded_queue_t;

/*
 * Initialize capacity in elements, not bytes. storage_size is the actual
 * writable byte extent at storage; bytes beyond capacity * element_size are
 * unused. All of storage, storage_size, element_size, and capacity must be
 * nonzero, and the storage must hold at least capacity elements.
 *
 * Returns OAMR_STATUS_INVALID for a NULL queue/storage, zero argument, product
 * overflow, or insufficient storage. A non-NULL queue is left zeroed and
 * uninitialized on failure. No bytes in storage are erased.
 */
oamr_status_t oamr_bounded_queue_init(oamr_bounded_queue_t *queue,
                                      void *storage,
                                      size_t storage_size,
                                      size_t element_size,
                                      size_t capacity);

/*
 * Push copies exactly one element. A full queue rejects the new element,
 * leaves existing elements unchanged, increments overflow_count saturating at
 * UINT64_MAX, and returns OAMR_STATUS_OVERFLOW. NULL element pointers and
 * uninitialized queues return OAMR_STATUS_INVALID without counting overflow.
 */
oamr_status_t oamr_bounded_queue_push(oamr_bounded_queue_t *queue,
                                      const void *element);

/*
 * Pop/peek copy the oldest element. Empty queues return OAMR_STATUS_EMPTY and
 * leave out_element untouched. Invalid arguments return OAMR_STATUS_INVALID.
 * Pop removes the element; peek preserves queue state.
 */
oamr_status_t oamr_bounded_queue_pop(oamr_bounded_queue_t *queue,
                                     void *out_element);
oamr_status_t oamr_bounded_queue_peek(const oamr_bounded_queue_t *queue,
                                      void *out_element);

/* Null/uninitialized/corrupted queues report zero from these accessors. */
size_t oamr_bounded_queue_count(const oamr_bounded_queue_t *queue);
size_t oamr_bounded_queue_capacity(const oamr_bounded_queue_t *queue);
uint64_t oamr_bounded_queue_overflow_count(const oamr_bounded_queue_t *queue);

/*
 * Reset indices, count, and overflow_count while retaining capacity/storage.
 * Stored bytes are not erased. Returns OAMR_STATUS_INVALID for a NULL,
 * uninitialized, or corrupted queue.
 */
oamr_status_t oamr_bounded_queue_reset(oamr_bounded_queue_t *queue);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_BOUNDED_QUEUE_H */
