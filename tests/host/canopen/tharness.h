/* SPDX-License-Identifier: MIT */
/* Minimal C test harness: no external dependency, runs under ASan and UBSan. */
#ifndef THARNESS_H
#define THARNESS_H

#include <stdio.h>
#include <string.h>

#include "oamr/co_frame.h"

extern int th_failures;
extern int th_checks;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        th_checks++;                                                                     \
        if (!(cond)) {                                                                   \
            th_failures++;                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);     \
        }                                                                                \
    } while (0)

#define CHECK_EQ(a, b)                                                                           \
    do {                                                                                         \
        th_checks++;                                                                             \
        unsigned long long th_a_ = (unsigned long long)(a);                                      \
        unsigned long long th_b_ = (unsigned long long)(b);                                      \
        if (th_a_ != th_b_) {                                                                    \
            th_failures++;                                                                       \
            fprintf(stderr, "%s:%d: CHECK_EQ failed: %s (0x%llx) != %s (0x%llx)\n", __FILE__,     \
                    __LINE__, #a, th_a_, #b, th_b_);                                             \
        }                                                                                        \
    } while (0)

typedef void (*th_fn)(void);
typedef struct {
    const char *name;
    th_fn fn;
} th_case;

int th_run(const char *suite, const th_case *cases, size_t n);

/* Build a frame from an id and up to 8 bytes. */
static inline oamr_co_frame_t th_frame(uint32_t id, uint8_t dlc, const uint8_t *bytes)
{
    oamr_co_frame_t f;
    memset(&f, 0, sizeof(f));
    f.id = id;
    f.dlc = dlc;
    if (bytes != NULL) {
        memcpy(f.data, bytes, dlc > 8u ? 8u : dlc);
    }
    return f;
}

/* Recording transmit stub for the scheduler. */
typedef struct {
    oamr_co_frame_t frames[64];
    unsigned n;
    unsigned fail_next; /* number of next sends to refuse */
} th_bus;

static inline bool th_send(void *ctx, const oamr_co_frame_t *f)
{
    th_bus *b = (th_bus *)ctx;
    if (b->fail_next > 0u) {
        b->fail_next--;
        return false;
    }
    if (b->n < 64u) {
        b->frames[b->n] = *f;
    }
    b->n++;
    return true;
}

#endif
