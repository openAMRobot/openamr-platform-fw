/* SPDX-License-Identifier: MIT */
/*
 * Static memory for the micro-ROS client: a bump arena in a static array,
 * installed as the rcutils default allocator before anything of micro-ROS is
 * called. Nothing here or in micro-ROS reaches the C heap (the image links
 * malloc and friends to a fatal stop, CMakeLists.txt, and the post-link
 * guard fails on a heap symbol).
 *
 * free() of a block is counted and the memory is not reused; the whole arena
 * is reset after the state machine has torn every entity down (DESTROY), so a
 * reconnect starts from an empty arena. An allocation that does not fit
 * returns NULL (rcl then reports an error and the state machine goes to
 * DESTROY); it is counted, never served from elsewhere. Pure C.
 */
#ifndef TLM_UROS_ALLOC_H
#define TLM_UROS_ALLOC_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint8_t *mem;
    size_t cap;
    size_t used;
    size_t high_water;  /* largest "used" since start, over all sessions */
    uint32_t allocs, frees, failures, resets;
} tlm_arena_t;

void tlm_arena_init(tlm_arena_t *a, void *mem, size_t cap);
void *tlm_arena_alloc(tlm_arena_t *a, size_t size);
void *tlm_arena_zalloc(tlm_arena_t *a, size_t n, size_t size);
void *tlm_arena_realloc(tlm_arena_t *a, void *p, size_t size);
void tlm_arena_free(tlm_arena_t *a, void *p);
void tlm_arena_reset(tlm_arena_t *a);

#endif /* TLM_UROS_ALLOC_H */
