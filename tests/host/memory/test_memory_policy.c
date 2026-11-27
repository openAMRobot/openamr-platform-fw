/* SPDX-License-Identifier: MIT */
/*
 * Portable arithmetic checks only.  These helpers are deliberately local to
 * this test: production placement evidence comes from the linked ELF and the
 * Python checker, not from a host-only memory abstraction.
 */
#include "test_harness.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool extent_valid(uintptr_t start, size_t size, uintptr_t limit)
{
    if (start > limit || size > (size_t)(limit - start)) {
        return false;
    }
    return true;
}

static bool extent_within(uintptr_t start, size_t size,
                          uintptr_t region_start, uintptr_t region_end)
{
    if (region_start > region_end || !extent_valid(start, size, UINTPTR_MAX)) {
        return false;
    }
    return start >= region_start && start + size <= region_end;
}

static bool extents_overlap(uintptr_t first_start, size_t first_size,
                            uintptr_t second_start, size_t second_size)
{
    if (!extent_valid(first_start, first_size, UINTPTR_MAX)
        || !extent_valid(second_start, second_size, UINTPTR_MAX)) {
        return true;
    }
    return first_start < second_start + second_size
        && second_start < first_start + first_size;
}

static void descriptor_extents(void)
{
    const uintptr_t sram1_start = ((uintptr_t)0x30000000u);
    const uintptr_t sram1_end = ((uintptr_t)0x30004000u);
    const uintptr_t rx = ((uintptr_t)0x30000000u);
    const uintptr_t tx = ((uintptr_t)0x30000060u);
    const size_t descriptor_size = (size_t)0x60u;

    CHECK(extent_within(rx, descriptor_size, sram1_start, sram1_end));
    CHECK(extent_within(tx, descriptor_size, sram1_start, sram1_end));
    CHECK(!extents_overlap(rx, descriptor_size, tx, descriptor_size));
    CHECK(extents_overlap(rx, (size_t)0x80u, tx, descriptor_size));
    CHECK(!extent_within(((uintptr_t)0x30003fc0u), descriptor_size, sram1_start, sram1_end));
    CHECK(!extent_within(((uintptr_t)0x30004000u), descriptor_size, sram1_start, sram1_end));
}

static void bounds_arithmetic(void)
{
    const uintptr_t sram2_start = ((uintptr_t)0x30004000u);
    const uintptr_t sram2_end = ((uintptr_t)0x30008000u);

    CHECK(extent_within(sram2_start, (size_t)0x4000u, sram2_start, sram2_end));
    CHECK(extent_within(sram2_end, (size_t)0u, sram2_start, sram2_end));
    CHECK(!extent_within(sram2_end + (uintptr_t)1u, (size_t)0u, sram2_start, sram2_end));
    CHECK(!extent_within(sram2_start, (size_t)0x4001u, sram2_start, sram2_end));
    CHECK(!extent_valid(UINTPTR_MAX, (size_t)1u, UINTPTR_MAX));
    CHECK(extent_valid(UINTPTR_MAX, (size_t)0u, UINTPTR_MAX));
    CHECK(!extent_within(sram2_start, (size_t)1u, sram2_end, sram2_start));
    CHECK(extents_overlap(UINTPTR_MAX, (size_t)1u, sram2_start, (size_t)1u));
    CHECK(extents_overlap(sram2_start, (size_t)1u, UINTPTR_MAX, (size_t)1u));
}

int main(void)
{
    RUN(descriptor_extents);
    RUN(bounds_arithmetic);
    return TEST_END();
}
