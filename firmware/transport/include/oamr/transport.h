#ifndef OAMR_TRANSPORT_H
#define OAMR_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OAMR_TRANSPORT_OK = 0,
    OAMR_TRANSPORT_WOULD_BLOCK = 1,
    OAMR_TRANSPORT_DISCONNECTED = 2,
    OAMR_TRANSPORT_INVALID = 3,
    OAMR_TRANSPORT_TIMEOUT = 4,
    OAMR_TRANSPORT_ERROR = 5
} oamr_transport_result_t;

/*
 * Nonblocking byte-stream callbacks.
 *
 * - No callback waits, polls until progress, or performs internal retries.
 * - transferred is required on every call. Whenever it is non-NULL, the
 *   callback sets it to zero on entry. Non-OK results leave it zero.
 * - For a zero-length request, data may be NULL; success reports zero bytes.
 * - For a nonzero request, data must be non-NULL. OK reports 1..requested
 *   bytes. Short transfers are successful and write consumes only that prefix.
 * - WOULD_BLOCK means no byte can be transferred immediately; callers may
 *   retry later. DISCONNECTED requires transport-specific reconnection.
 * - TIMEOUT reports an already-bounded adapter transaction expiring; it never
 *   permits the callback itself to block waiting for a deadline.
 * - NULL required pointers and invalid adapter state return INVALID.
 * - Buffer/transferred pointers are borrowed only for the call and must not be
 *   retained. The caller retains all storage ownership.
 */
typedef oamr_transport_result_t (*oamr_transport_read_callback_t)(
    void *context,
    uint8_t *data,
    size_t data_capacity,
    size_t *transferred);

typedef oamr_transport_result_t (*oamr_transport_write_callback_t)(
    void *context,
    const uint8_t *data,
    size_t data_size,
    size_t *transferred);

/* context is adapter-owned and passed through unchanged; it may be NULL when
 * the adapter permits it. A NULL callback denotes an unsupported operation
 * and must not be invoked. This binding performs no allocation or lifecycle
 * management. */
typedef struct {
    void *context;
    oamr_transport_read_callback_t read;
    oamr_transport_write_callback_t write;
} oamr_transport_t;

#ifdef __cplusplus
}
#endif

#endif /* OAMR_TRANSPORT_H */
