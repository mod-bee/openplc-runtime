// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Autonomy®

#include "debug_read_guard.h"

#include <sched.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "image_tables.h"

/* Scratch for the second read of a value; a larger one goes to the heap. */
#define SCRATCH_BYTES 4096

static int64_t now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

static bool lock_exports(void)
{
    return ext_strucpp_debug_global_index != NULL && ext_strucpp_global_try_lock != NULL &&
           ext_strucpp_global_unlock != NULL;
}

/* The leaf's global, locked: its index, or -1 when it is no global or its lock
 * did not come within DEBUG_READ_GLOBAL_WAIT_US. */
static int32_t lock_global(uint8_t arr, uint16_t elem)
{
    const int32_t g = ext_strucpp_debug_global_index(arr, elem);
    if (g < 0) return -1;
    const int64_t until = now_us() + DEBUG_READ_GLOBAL_WAIT_US;
    while (!ext_strucpp_global_try_lock((uint32_t)g))
    {
        if (now_us() >= until) return -1;
        sched_yield();
    }
    return g;
}

uint16_t debug_read_guarded(uint8_t arr, uint16_t elem, uint8_t *dest)
{
    if (ext_strucpp_debug_read == NULL) return 0;
    if (!lock_exports()) return ext_strucpp_debug_read(arr, elem, dest);

    const int32_t g = lock_global(arr, elem);
    if (g >= 0)
    {
        const uint16_t n = ext_strucpp_debug_read(arr, elem, dest);
        ext_strucpp_global_unlock((uint32_t)g);
        return n;
    }

    uint8_t      again[512];
    const size_t size = ext_strucpp_debug_size ? ext_strucpp_debug_size(arr, elem) : 0;
    if (size == 0 || size > sizeof again) return ext_strucpp_debug_read(arr, elem, dest);
    uint16_t n = 0;
    for (int t = 0; t < DEBUG_READ_TRIES; ++t)
    {
        n = ext_strucpp_debug_read(arr, elem, dest);
        if (n == 0 || n > sizeof again) return n;
        if (ext_strucpp_debug_read(arr, elem, again) == n && memcmp(dest, again, n) == 0) return n;
    }
    return n;
}

static bool same_chunk(const plc_text_chunk_t *a, const plc_text_chunk_t *b)
{
    return a->status == b->status && a->tag == b->tag && a->flags == b->flags &&
           a->cap == b->cap && a->len == b->len && a->count == b->count;
}

uint8_t debug_read_text_guarded(uint8_t arr, uint16_t elem, uint16_t offset, uint16_t max,
                                uint8_t *data, uint16_t data_cap, plc_text_chunk_t *info)
{
    if (ext_strucpp_debug_read_text == NULL)
    {
        memset(info, 0, sizeof(*info));
        info->status = 0x83;
        return info->status;
    }
    if (!lock_exports())
        return ext_strucpp_debug_read_text(arr, elem, offset, max, data, data_cap, info);

    const int32_t g = lock_global(arr, elem);
    if (g >= 0)
    {
        const uint8_t st = ext_strucpp_debug_read_text(arr, elem, offset, max, data, data_cap, info);
        ext_strucpp_global_unlock((uint32_t)g);
        return st;
    }

    uint8_t  stack[SCRATCH_BYTES];
    uint8_t *again = data_cap <= sizeof stack ? stack : (uint8_t *)malloc(data_cap);
    if (again == NULL) return ext_strucpp_debug_read_text(arr, elem, offset, max, data, data_cap, info);
    plc_text_chunk_t other;
    uint8_t          st = 0;
    for (int t = 0; t < DEBUG_READ_TRIES; ++t)
    {
        st = ext_strucpp_debug_read_text(arr, elem, offset, max, data, data_cap, info);
        if (st != 0x7E) break;
        ext_strucpp_debug_read_text(arr, elem, offset, max, again, data_cap, &other);
        const size_t bytes = (size_t)info->count * (info->tag == PLC_TEXT_TAG_WSTRING ? 2u : 1u);
        if (same_chunk(info, &other) && bytes <= data_cap && memcmp(data, again, bytes) == 0) break;
    }
    if (again != stack) free(again);
    return st;
}
