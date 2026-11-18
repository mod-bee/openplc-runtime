// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Autonomy®

/*
 * Debug reads of program variables (debug_read_guard.c), for FC 0x43/0x44/0x4F
 * and the plugins' debug_read / debug_read_text:
 *   - a global is read under its own lock, taken by try-lock and retry;
 *   - a global whose lock does not come in time is not waited on: it is read
 *     until two reads agree, as a task-local variable is;
 *   - a task-local variable written mid-read is read again until two agree;
 *   - a program without the lock exports is read once, as before.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "debug_read_guard.h"
#include "image_tables.h"

/* ---- the program's exports ----------------------------------------------- */

uint16_t (*ext_strucpp_debug_size)(uint8_t, uint16_t)            = NULL;
uint16_t (*ext_strucpp_debug_read)(uint8_t, uint16_t, uint8_t *) = NULL;
plc_debug_read_text_func_t ext_strucpp_debug_read_text           = NULL;
int32_t (*ext_strucpp_debug_global_index)(uint8_t, uint16_t)     = NULL;
bool (*ext_strucpp_global_try_lock)(uint32_t)                    = NULL;
void (*ext_strucpp_global_unlock)(uint32_t)                      = NULL;

/* elem 0: a global (index 4); elem 1: task-local. */
static int  g_reads, g_torn, g_busy, g_try, g_unlocks;
static bool g_locked, g_read_unlocked_global;

static int32_t stub_global_index(uint8_t arr, uint16_t elem) { return elem == 0 ? 4 : -1; }
static bool    stub_try_lock(uint32_t g)
{
    ++g_try;
    if (g != 4 || g_busy < 0 || g_try <= g_busy) return false;  /* g_busy < 0: never */
    g_locked = true;
    return true;
}
static void stub_unlock(uint32_t g) { g_locked = false; ++g_unlocks; }

static uint16_t stub_size(uint8_t arr, uint16_t elem) { return 4; }
static uint16_t stub_read(uint8_t arr, uint16_t elem, uint8_t *dest)
{
    ++g_reads;
    if (elem == 0 && !g_locked) g_read_unlocked_global = true;
    uint32_t v = 0x11223344u;
    if (g_reads <= g_torn) v ^= (uint32_t)g_reads;  /* the scan wrote mid-read */
    memcpy(dest, &v, 4);
    return 4;
}

static uint8_t stub_read_text(uint8_t arr, uint16_t elem, uint16_t offset, uint16_t max,
                              uint8_t *data, uint16_t data_cap, plc_text_chunk_t *info)
{
    ++g_reads;
    if (elem == 0 && !g_locked) g_read_unlocked_global = true;
    memset(info, 0, sizeof(*info));
    uint16_t n = max < data_cap ? max : data_cap;
    if (n > 300) n = 300;
    for (uint16_t i = 0; i < n; ++i) data[i] = (uint8_t)('a' + (offset + i) % 26);
    if (g_reads <= g_torn && n > 0) data[n / 2] = (uint8_t)('0' + g_reads); /* each read torn differently */
    info->status = 0x7E;
    info->tag    = PLC_TEXT_TAG_STRING;
    info->cap    = 320;
    info->len    = 300;
    info->count  = n;
    return 0x7E;
}

/* ---- harness --------------------------------------------------------------- */

static int         g_fail;
static const char *g_case;
#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            printf("  FAIL  %s: %s (line %d)\n", g_case, msg, __LINE__);                           \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

static void reset(bool exports, int busy, int torn)
{
    ext_strucpp_debug_size         = stub_size;
    ext_strucpp_debug_read         = stub_read;
    ext_strucpp_debug_read_text    = stub_read_text;
    ext_strucpp_debug_global_index = exports ? stub_global_index : NULL;
    ext_strucpp_global_try_lock    = exports ? stub_try_lock : NULL;
    ext_strucpp_global_unlock      = exports ? stub_unlock : NULL;
    g_reads = g_try = g_unlocks = 0;
    g_busy  = busy;
    g_torn  = torn;
    g_locked = g_read_unlocked_global = false;
}

static bool clean(const uint8_t *d, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (d[i] < 'a' || d[i] > 'z') return false;
    return true;
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void case_scalar(void)
{
    uint8_t  d[4];
    uint32_t v;

    g_case = "no lock exports: one read, as before";
    reset(false, 0, 0);
    CHECK(debug_read_guarded(0, 1, d) == 4 && g_reads == 1, "one read");

    g_case = "a global is read under its own lock";
    reset(true, 3, 0);
    CHECK(debug_read_guarded(0, 0, d) == 4, "read");
    CHECK(g_try == 4 && g_reads == 1 && !g_read_unlocked_global, "try-locked, read once under the lock");
    CHECK(g_unlocks == 1 && !g_locked, "released");

    g_case = "a global whose lock never comes is not waited on";
    reset(true, -1, 1);
    double t0 = now_ms();
    CHECK(debug_read_guarded(0, 0, d) == 4, "read anyway");
    CHECK(now_ms() - t0 < 50.0, "bounded wait");
    memcpy(&v, d, 4);
    CHECK(v == 0x11223344u && g_reads >= 3, "read until two reads agree");
    CHECK(g_unlocks == 0, "nothing to release");

    g_case = "a task-local variable written mid-read is read again";
    reset(true, 0, 1);
    CHECK(debug_read_guarded(0, 1, d) == 4, "read");
    memcpy(&v, d, 4);
    CHECK(v == 0x11223344u && g_reads >= 3, "two reads agree");
    CHECK(g_try == 0, "no lock for a task-local variable");
}

static void case_text(void)
{
    uint8_t          data[512];
    plc_text_chunk_t info;

    g_case = "text, no lock exports: one read";
    reset(false, 0, 0);
    CHECK(debug_read_text_guarded(0, 1, 0, 300, data, sizeof data, &info) == 0x7E && g_reads == 1,
          "one read");

    g_case = "text, a global chunk under its own lock";
    reset(true, 2, 0);
    CHECK(debug_read_text_guarded(0, 0, 0, 300, data, sizeof data, &info) == 0x7E, "read");
    CHECK(g_reads == 1 && !g_read_unlocked_global && g_unlocks == 1, "once, under the lock");

    g_case = "text, a task-local chunk written mid-read";
    reset(true, 0, 2);
    CHECK(debug_read_text_guarded(0, 1, 0, 300, data, sizeof data, &info) == 0x7E, "read");
    CHECK(info.count == 300 && clean(data, 300) && g_reads >= 4, "two reads agree");

    g_case = "text, a chunk larger than the stack scratch";
    reset(true, 0, 1);
    static uint8_t big[8192];
    CHECK(debug_read_text_guarded(0, 1, 0, 300, big, sizeof big, &info) == 0x7E, "read");
    CHECK(clean(big, 300), "still compared");
}

int main(void)
{
    case_scalar();
    case_text();
    if (g_fail)
    {
        printf("test_debug_read_guard: %d failed\n", g_fail);
        return 1;
    }
    printf("test_debug_read_guard: all passed\n");
    return 0;
}
