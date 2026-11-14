/* SPDX-License-Identifier: MIT */
/*
 * Configuration read-back comparator.
 *
 * Compares values read from a device (scheduler samples) against expected
 * values supplied by the caller and reports every mismatch. Input for the
 * future DRIVE_CONFIG_MISMATCH fault; this module raises no fault itself and
 * never writes to the device. It does not know any expected value: the
 * owner's commissioning record (decision D-24) supplies them.
 *
 * An entry with no read, a failed read or a stale read is reported as
 * UNVERIFIED, never as a match.
 */
#ifndef OAMR_CONFIG_READBACK_H
#define OAMR_CONFIG_READBACK_H

#include <stdbool.h>
#include <stdint.h>

#include "oamr/co_upload_sched.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OAMR_RB_MAX_ENTRIES 16u

typedef struct {
    uint16_t index;
    uint8_t subindex;
    uint8_t size;        /* expected byte size, 1, 2 or 4 */
    uint32_t mask;       /* bits compared; 0 is rejected as a configuration error */
    uint32_t expected;   /* compared under mask */
} oamr_rb_expect_t;

typedef enum {
    OAMR_RB_MATCH = 0,
    OAMR_RB_MISMATCH,       /* read value differs from expected under mask */
    OAMR_RB_SIZE_MISMATCH,  /* server stated another size */
    OAMR_RB_UNVERIFIED,     /* no valid, fresh read */
    OAMR_RB_NOT_IN_LIST,    /* the object is not in the scheduler list */
    OAMR_RB_BAD_EXPECT      /* mask 0 or size not 1, 2, 4 */
} oamr_rb_result_t;

typedef struct {
    oamr_rb_result_t result;
    uint32_t read_value;    /* 0 unless the read was valid */
    uint8_t read_size;
} oamr_rb_entry_report_t;

typedef struct {
    bool configured;
    uint8_t n;
    uint8_t mismatches;     /* MISMATCH and SIZE_MISMATCH */
    uint8_t unverified;     /* UNVERIFIED, NOT_IN_LIST and BAD_EXPECT */
    bool all_match;         /* n > 0, configured, every entry MATCH */
    oamr_rb_entry_report_t entries[OAMR_RB_MAX_ENTRIES];
} oamr_rb_report_t;

/* Compare expectations against the samples of sched. max_age_us has no
 * default; 0 makes the report NOT configured (all_match false). A sample
 * read at sample_us with now_us - sample_us > max_age_us is UNVERIFIED. */
void oamr_rb_compare(const oamr_rb_expect_t *expect, uint8_t n_expect, const oamr_co_upl_t *sched,
                     uint64_t max_age_us, uint64_t now_us, oamr_rb_report_t *out);

const char *oamr_rb_result_name(oamr_rb_result_t r);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_CONFIG_READBACK_H */
