/* SPDX-License-Identifier: MIT */
/*
 * Read-only SDO upload scheduler.
 *
 * Cycles through a caller-supplied list of objects (index, subindex) on one
 * node, one expedited upload at a time, with a per-transfer timeout and a
 * retry limit. It can only send initiate-upload requests (byte 0 = 40h): the
 * only encoder it calls is oamr_co_sdo_encode_upload_request(). It sends no
 * SDO abort on timeout either; an expedited upload leaves no open transfer on
 * the server, and the next request starts a new one.
 *
 * Every entry keeps its own sample with its own validity. A sample is valid
 * only after an OK response to the request issued for that entry; a timeout,
 * abort or protocol error makes it invalid at once. The previous value is
 * never presented as a new valid sample: seq increases only on a new OK.
 *
 * No default for anything: the caller passes node id, list, timeout and
 * retry limit; any of them missing leaves the scheduler NOT_CONFIGURED and
 * tick() sends nothing.
 *
 * Late responses after a retry (late_guard_us). An expedited SDO response
 * carries no transaction id: the answer to a timed-out request A and the
 * answer to its retry B for the same object are identical on the bus. After
 * a retry has been sent, every response for the current object (OK or abort
 * echoing its index and subindex) that arrives less than late_guard_us after
 * that retry was sent is counted in `late` and never becomes a sample; the
 * scheduler keeps waiting. A response arriving at or after the guard is
 * taken as the answer to B. Limitation: an answer to A that is delayed beyond
 * the guard is still indistinguishable from B's answer and is accepted. The
 * guard applies to retries only, not to the first attempt of an entry.
 * late_guard_us has no default. It must be > 0 and < timeout_us whenever
 * max_retries > 0, otherwise init() returns NOT_CONFIGURED; with
 * max_retries = 0 there is no retry and the value is ignored.
 *
 * Health (reporting only). oamr_co_upl_health() gives an overall state:
 * UNAVAILABLE after health_max_consecutive_protocol_errors protocol errors
 * in a row (malformed, segmented or wrong-specifier answers; an OK sample
 * resets the run), or when health_max_no_valid_cycle_us has passed without
 * one complete valid cycle (a pass over the whole list in which every entry
 * ended with an OK sample), counted from the first tick or the last complete
 * valid cycle. A clock that goes backwards counts as the time being exceeded.
 * UNAVAILABLE stays until the next complete valid cycle. Both parameters have
 * no default; 0 disables that criterion, and the health report says which
 * criteria are disabled. With both 0 the state is DISABLED. The health state
 * never changes what the scheduler sends and must not gate motion.
 */
#ifndef OAMR_CO_UPLOAD_SCHED_H
#define OAMR_CO_UPLOAD_SCHED_H

#include <stdbool.h>
#include <stdint.h>

#include "oamr/co_frame.h"
#include "oamr/co_sdo.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define OAMR_CO_UPL_MAX_ENTRIES 16u

    typedef struct
    {
        uint16_t index;
        uint8_t subindex;
    } oamr_co_object_t;

    typedef enum
    {
        OAMR_CO_SAMPLE_NONE = 0, /* never read */
        OAMR_CO_SAMPLE_VALID,
        OAMR_CO_SAMPLE_TIMEOUT,  /* retries exhausted without an answer */
        OAMR_CO_SAMPLE_ABORTED,  /* server answered with an SDO abort */
        OAMR_CO_SAMPLE_PROTOCOL, /* malformed, segmented or wrong specifier answer */
        OAMR_CO_SAMPLE_TX_FAILED /* request could not be queued, retries exhausted */
    } oamr_co_sample_status_t;

    typedef struct
    {
        oamr_co_sample_status_t status;
        uint32_t value; /* meaningful only when status == VALID */
        uint8_t size;   /* bytes stated by the server */
        bool size_indicated;
        uint64_t sample_us;  /* local receipt time of the OK response */
        uint32_t seq;        /* increments on every new VALID sample, starts at 0 = none */
        uint32_t abort_code; /* last abort code for this entry */
    } oamr_co_sample_t;

    typedef struct
    {
        uint32_t ok;
        uint32_t timeouts; /* individual request timeouts, retries included */
        uint32_t retries;
        uint32_t aborts;
        uint32_t protocol_errors;
        uint32_t tx_failures;
        uint32_t failed; /* transfers given up (status not VALID) */
        uint32_t late;   /* responses for this entry inside the guard after a retry */
    } oamr_co_upl_counters_t;

    typedef struct
    {
        uint8_t node_id;                 /* 1..127; no default */
        const oamr_co_object_t *objects; /* caller-owned, must outlive the scheduler */
        uint8_t n_objects;               /* 1..OAMR_CO_UPL_MAX_ENTRIES */
        uint64_t timeout_us;             /* per request; no default, 0 = not configured */
        uint8_t max_retries;             /* extra attempts after the first; 0 is a valid choice */
        bool max_retries_set;            /* must be true: makes "0 retries" an explicit choice */
        uint64_t late_guard_us;          /* after a retry; no default; see file header */
        uint32_t health_max_consecutive_protocol_errors; /* N; no default, 0 = criterion disabled */
        uint64_t health_max_no_valid_cycle_us;           /* no default, 0 = criterion disabled */
        oamr_co_send_fn send;
        void *send_ctx;
    } oamr_co_upl_config_t;

    typedef enum
    {
        OAMR_CO_UPL_NOT_CONFIGURED = 0,
        OAMR_CO_UPL_IDLE, /* nothing outstanding; next tick starts a request */
        OAMR_CO_UPL_BUSY  /* one request outstanding */
    } oamr_co_upl_state_t;

    typedef enum
    {
        OAMR_CO_UPL_HEALTH_DISABLED = 0, /* both criteria unset, or scheduler not configured */
        OAMR_CO_UPL_HEALTH_STARTING,     /* no complete valid cycle yet, no criterion tripped */
        OAMR_CO_UPL_HEALTH_AVAILABLE,    /* at least one complete valid cycle, none tripped */
        OAMR_CO_UPL_HEALTH_UNAVAILABLE   /* a criterion tripped; until the next valid cycle */
    } oamr_co_upl_health_state_t;

/* Bits of oamr_co_upl_health_t.reasons. */
#define OAMR_CO_UPL_HEALTH_R_PROTOCOL 0x01u /* N consecutive protocol errors */
#define OAMR_CO_UPL_HEALTH_R_NO_CYCLE 0x02u /* no complete valid cycle within the limit */

    typedef struct
    {
        oamr_co_upl_health_state_t state;
        uint8_t reasons;                 /* OAMR_CO_UPL_HEALTH_R_* while UNAVAILABLE */
        bool protocol_criterion_enabled; /* false: N unset, criterion disabled */
        bool cycle_criterion_enabled;    /* false: time unset, criterion disabled */
        uint32_t consecutive_protocol_errors;
        uint32_t unavailable_events; /* transitions into UNAVAILABLE */
        uint32_t valid_cycles;       /* complete valid cycles */
        bool have_ref;               /* ref_us set (first tick seen) */
        uint64_t ref_us;             /* first tick or end of the last complete valid cycle */
    } oamr_co_upl_health_t;

    typedef struct
    {
        oamr_co_upl_config_t cfg;
        oamr_co_upl_state_t state;
        uint8_t cur;     /* entry being read or next to read */
        uint8_t attempt; /* attempts made for the current entry */
        uint64_t sent_us;
        uint32_t cycles;       /* completed passes over the whole list */
        uint32_t unsolicited;  /* SDO responses from the node with nothing outstanding */
        uint32_t wrong_object; /* responses echoing another object (late or duplicate) */
        uint32_t late;         /* responses inside the guard after a retry, all entries */
        bool cycle_all_valid;  /* every entry of the current pass ended VALID so far */
        oamr_co_upl_health_t health;
        oamr_co_sample_t samples[OAMR_CO_UPL_MAX_ENTRIES];
        oamr_co_upl_counters_t counters[OAMR_CO_UPL_MAX_ENTRIES];
    } oamr_co_upl_t;

    oamr_co_upl_state_t oamr_co_upl_init(oamr_co_upl_t *s, const oamr_co_upl_config_t *cfg);

    /* Call periodically. Starts the next request when idle, handles timeouts and
     * retries when busy. At most one frame is sent per call. */
    void oamr_co_upl_tick(oamr_co_upl_t *s, uint64_t now_us);

    /* Feed every received frame. Returns true if it was an SDO response from the
     * scheduler's node (whether used or not). */
    bool oamr_co_upl_on_frame(oamr_co_upl_t *s, const oamr_co_frame_t *f, uint64_t now_us);

    /* Sample of entry i, or NULL for an out-of-range i or an unconfigured scheduler. */
    const oamr_co_sample_t *oamr_co_upl_sample(const oamr_co_upl_t *s, uint8_t i);
    const oamr_co_upl_counters_t *oamr_co_upl_counters(const oamr_co_upl_t *s, uint8_t i);

    /* Overall health, as of the last tick() or on_frame(). Never NULL. */
    const oamr_co_upl_health_t *oamr_co_upl_health(const oamr_co_upl_t *s);
    /* "DISABLED", "STARTING", "AVAILABLE", "UNAVAILABLE"; "unknown" for others. */
    const char *oamr_co_upl_health_name(oamr_co_upl_health_state_t st);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_CO_UPLOAD_SCHED_H */
