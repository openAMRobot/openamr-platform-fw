#ifndef OAMR_STATUS_H
#define OAMR_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Common operation and data-status vocabulary.
 *
 * OAMR_STATUS_INVALID is zero so a zero-initialized status is not silently
 * healthy. OAMR_STATUS_OK reports explicit success/validity; by itself it does
 * not establish freshness.
 */
typedef enum {
    OAMR_STATUS_INVALID = 0,
    OAMR_STATUS_OK = 1,
    OAMR_STATUS_EMPTY = 2,
    OAMR_STATUS_STALE = 3,
    OAMR_STATUS_TIMEOUT = 4,
    OAMR_STATUS_OVERFLOW = 5,
    OAMR_STATUS_ERROR = 6
} oamr_status_t;

#ifdef __cplusplus
}
#endif

#endif /* OAMR_STATUS_H */
