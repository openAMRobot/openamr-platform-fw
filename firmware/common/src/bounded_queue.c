#include "oamr/bounded_queue.h"

static bool oamr_bounded_queue_is_valid(const oamr_bounded_queue_t *queue)
{
    size_t required_size;

    if (queue == NULL || !queue->initialized || queue->storage == NULL ||
        queue->element_size == 0u || queue->capacity == 0u ||
        queue->count > queue->capacity || queue->head >= queue->capacity ||
        queue->tail >= queue->capacity) {
        return false;
    }

    if (queue->element_size > SIZE_MAX / queue->capacity) {
        return false;
    }

    required_size = queue->element_size * queue->capacity;
    return queue->storage_size >= required_size;
}

static void oamr_bounded_queue_clear(oamr_bounded_queue_t *queue)
{
    queue->storage = NULL;
    queue->storage_size = 0u;
    queue->element_size = 0u;
    queue->capacity = 0u;
    queue->head = 0u;
    queue->tail = 0u;
    queue->count = 0u;
    queue->overflow_count = 0u;
    queue->initialized = false;
}

oamr_status_t oamr_bounded_queue_init(oamr_bounded_queue_t *queue,
                                      void *storage,
                                      size_t storage_size,
                                      size_t element_size,
                                      size_t capacity)
{
    size_t required_size;

    if (queue == NULL) {
        return OAMR_STATUS_INVALID;
    }

    oamr_bounded_queue_clear(queue);

    if (storage == NULL || storage_size == 0u || element_size == 0u ||
        capacity == 0u || element_size > SIZE_MAX / capacity) {
        return OAMR_STATUS_INVALID;
    }

    required_size = element_size * capacity;
    if (storage_size < required_size) {
        return OAMR_STATUS_INVALID;
    }

    queue->storage = (uint8_t *)storage;
    queue->storage_size = storage_size;
    queue->element_size = element_size;
    queue->capacity = capacity;
    queue->initialized = true;
    return OAMR_STATUS_OK;
}

oamr_status_t oamr_bounded_queue_push(oamr_bounded_queue_t *queue,
                                      const void *element)
{
    uint8_t *destination;
    size_t i;
    const uint8_t *source;

    if (!oamr_bounded_queue_is_valid(queue) || element == NULL) {
        return OAMR_STATUS_INVALID;
    }

    if (queue->count == queue->capacity) {
        if (queue->overflow_count < UINT64_MAX) {
            ++queue->overflow_count;
        }
        return OAMR_STATUS_OVERFLOW;
    }

    destination = queue->storage + (queue->tail * queue->element_size);
    source = (const uint8_t *)element;
    for (i = 0u; i < queue->element_size; ++i) {
        destination[i] = source[i];
    }

    ++queue->tail;
    if (queue->tail == queue->capacity) {
        queue->tail = 0u;
    }
    ++queue->count;
    return OAMR_STATUS_OK;
}

oamr_status_t oamr_bounded_queue_peek(const oamr_bounded_queue_t *queue,
                                      void *out_element)
{
    const uint8_t *source;
    uint8_t *destination;
    size_t i;

    if (!oamr_bounded_queue_is_valid(queue) || out_element == NULL) {
        return OAMR_STATUS_INVALID;
    }

    if (queue->count == 0u) {
        return OAMR_STATUS_EMPTY;
    }

    source = queue->storage + (queue->head * queue->element_size);
    destination = (uint8_t *)out_element;
    for (i = 0u; i < queue->element_size; ++i) {
        destination[i] = source[i];
    }
    return OAMR_STATUS_OK;
}

oamr_status_t oamr_bounded_queue_pop(oamr_bounded_queue_t *queue,
                                     void *out_element)
{
    oamr_status_t status = oamr_bounded_queue_peek(queue, out_element);

    if (status != OAMR_STATUS_OK) {
        return status;
    }

    ++queue->head;
    if (queue->head == queue->capacity) {
        queue->head = 0u;
    }
    --queue->count;
    return OAMR_STATUS_OK;
}

size_t oamr_bounded_queue_count(const oamr_bounded_queue_t *queue)
{
    return oamr_bounded_queue_is_valid(queue) ? queue->count : 0u;
}

size_t oamr_bounded_queue_capacity(const oamr_bounded_queue_t *queue)
{
    return oamr_bounded_queue_is_valid(queue) ? queue->capacity : 0u;
}

uint64_t oamr_bounded_queue_overflow_count(const oamr_bounded_queue_t *queue)
{
    return oamr_bounded_queue_is_valid(queue) ? queue->overflow_count : 0u;
}

oamr_status_t oamr_bounded_queue_reset(oamr_bounded_queue_t *queue)
{
    if (!oamr_bounded_queue_is_valid(queue)) {
        return OAMR_STATUS_INVALID;
    }

    queue->head = 0u;
    queue->tail = 0u;
    queue->count = 0u;
    queue->overflow_count = 0u;
    return OAMR_STATUS_OK;
}
