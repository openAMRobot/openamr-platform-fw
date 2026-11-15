/* SPDX-License-Identifier: MIT */
#include "tlm_uros_alloc.h"

#include <string.h>

/* Every block starts with its size, 8-byte aligned (the largest alignment
 * micro-ROS needs on the Cortex-M7: uint64_t, double). */
#define ALIGN 8u
#define HDR ALIGN

static size_t round_up(size_t n)
{
    return (n + (ALIGN - 1u)) & ~(size_t)(ALIGN - 1u);
}

void tlm_arena_init(tlm_arena_t *a, void *mem, size_t cap)
{
    memset(a, 0, sizeof(*a));
    const uintptr_t start = ((uintptr_t)mem + (ALIGN - 1u)) & ~(uintptr_t)(ALIGN - 1u);
    const size_t lost = (size_t)(start - (uintptr_t)mem);
    a->mem = (uint8_t *)start;
    a->cap = cap > lost ? (cap - lost) & ~(size_t)(ALIGN - 1u) : 0u;
}

void *tlm_arena_alloc(tlm_arena_t *a, size_t size)
{
    if (size == 0u || size > a->cap)
    {
        a->failures++;
        return NULL;
    }
    const size_t need = HDR + round_up(size);
    if (need > a->cap - a->used)
    {
        a->failures++;
        return NULL;
    }
    uint8_t *block = a->mem + a->used;
    memcpy(block, &size, sizeof(size));
    a->used += need;
    if (a->used > a->high_water)
    {
        a->high_water = a->used;
    }
    a->allocs++;
    return block + HDR;
}

void *tlm_arena_zalloc(tlm_arena_t *a, size_t n, size_t size)
{
    if (size != 0u && n > (size_t)-1 / size)
    {
        a->failures++;
        return NULL;
    }
    void *p = tlm_arena_alloc(a, n * size);
    if (p != NULL)
    {
        memset(p, 0, n * size);
    }
    return p;
}

void *tlm_arena_realloc(tlm_arena_t *a, void *p, size_t size)
{
    if (p == NULL)
    {
        return tlm_arena_alloc(a, size);
    }
    size_t old;
    memcpy(&old, (uint8_t *)p - HDR, sizeof(old));
    if (size <= old)
    {
        return p;
    }
    void *q = tlm_arena_alloc(a, size);
    if (q != NULL)
    {
        memcpy(q, p, old);
        tlm_arena_free(a, p);
    }
    return q;
}

void tlm_arena_free(tlm_arena_t *a, void *p)
{
    if (p != NULL)
    {
        a->frees++;
    }
}

void tlm_arena_reset(tlm_arena_t *a)
{
    a->used = 0u;
    a->resets++;
}
