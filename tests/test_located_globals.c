// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Autonomy®

/* Unit tests for located_globals_join_ex(). Pin: join is by pointer
 * identity, order-independent; out_matched<globals_count signals the
 * generated arrays disagree; NULLs are skipped; empty globals → 0. */

#include <stdio.h>
#include <stddef.h>

#include "located_globals.h"
#include "unity.h"

#define CHECK(cond, msg, ...)                                                  \
    do {                                                                       \
        if (!(cond)) {                                                         \
            char why_[256];                                                    \
            snprintf(why_, sizeof why_, msg, ##__VA_ARGS__);                   \
            TEST_FAIL_MESSAGE(why_);                                           \
        }                                                                      \
    } while (0)

#define MAX_VARS 8
static char  g_storage[MAX_VARS];          /* distinct addresses */
static const void *g_lv[MAX_VARS];         /* the "locatedVars[i].pointer" values */

static const void *pointer_at(const void *handle, uint32_t index)
{
    const void *const *lv = (const void *const *)handle;
    return lv[index];
}

/* Convenience: address of dummy storage slot n. */
#define S(n) ((const void *)&g_storage[n])

static void reset(void)
{
    for (uint32_t i = 0; i < MAX_VARS; ++i) g_lv[i] = NULL;
}

void test_globals_first(void)
{
    reset();
    g_lv[0] = S(0);   /* GLOBALVAR  AT %MD0    — config global */
    g_lv[1] = S(1);   /* GLOBALBOOL AT %MX0.0  — config global */
    g_lv[2] = S(2);   /* HW_IN      AT %IX0.1  — program-local */
    const void *globals[] = { S(0), S(1) };

    uint32_t idx[3], matched = 0;
    uint32_t n = located_globals_join_ex(3, g_lv, pointer_at, globals, 2,
                                         idx, &matched);

    CHECK(n == 2, "expected 2 config globals, got %u", n);
    CHECK(idx[0] == 0 && idx[1] == 1, "expected [0,1], got [%u,%u]", idx[0], idx[1]);
    CHECK(matched == 2, "expected 2 matched globals, got %u", matched);
}

/* ---------------------------------------------------------------------------
 * Order independence — the property the old code lacked.
 * ------------------------------------------------------------------------- */
void test_ordering_independence(void)
{

    /* (a) globals last */
    {
        reset();
        g_lv[0] = S(0);            /* program-local */
        g_lv[1] = S(1);
        g_lv[2] = S(2);
        const void *globals[] = { S(1), S(2) };
        uint32_t idx[3], matched = 0;
        uint32_t n = located_globals_join_ex(3, g_lv, pointer_at, globals, 2,
                                             idx, &matched);
        CHECK(n == 2, "(globals last) expected 2, got %u", n);
        CHECK(idx[0] == 1 && idx[1] == 2, "(globals last) got [%u,%u]", idx[0], idx[1]);
    }

    /* (b) interleaved */
    {
        reset();
        for (uint32_t i = 0; i < 4; ++i) g_lv[i] = S(i);
        const void *globals[] = { S(0), S(2) };
        uint32_t idx[4], matched = 0;
        uint32_t n = located_globals_join_ex(4, g_lv, pointer_at, globals, 2,
                                             idx, &matched);
        CHECK(n == 2, "(interleaved) expected 2, got %u", n);
        CHECK(idx[0] == 0 && idx[1] == 2, "(interleaved) got [%u,%u]", idx[0], idx[1]);
    }
}

void test_multi_program(void)
{
    reset();
    for (uint32_t i = 0; i < 7; ++i) g_lv[i] = S(i);
    const void *globals[] = { S(0), S(1), S(2) };

    uint32_t idx[7], matched = 0;
    uint32_t n = located_globals_join_ex(7, g_lv, pointer_at, globals, 3,
                                         idx, &matched);
    CHECK(n == 3, "expected 3 config globals, got %u", n);
    CHECK(idx[0] == 0 && idx[1] == 1 && idx[2] == 2,
          "expected [0,1,2], got [%u,%u,%u]", idx[0], idx[1], idx[2]);
    CHECK(matched == 3, "expected 3 matched, got %u", matched);
}

/* ------------------------------------------------------------------------- */
void test_degradation_and_edges(void)
{

    /* Older program: no globals array at all (accessors absent). The caller
     * warns and services no globals; the join must simply report zero. */
    {
        reset();
        for (uint32_t i = 0; i < 3; ++i) g_lv[i] = S(i);
        uint32_t idx[3], matched = 99;
        CHECK(located_globals_join_ex(3, g_lv, pointer_at, NULL, 0, idx, &matched) == 0,
              "NULL globals array should yield 0");
        CHECK(matched == 0, "matched should be 0 for a NULL globals array");
    }

    /* Present but empty: project genuinely has no located globals. Distinct from
     * the case above at the caller (no warning), identical here. */
    {
        reset();
        for (uint32_t i = 0; i < 3; ++i) g_lv[i] = S(i);
        const void *globals[] = { NULL };
        uint32_t idx[3], matched = 99;
        CHECK(located_globals_join_ex(3, g_lv, pointer_at, globals, 0, idx, &matched) == 0,
              "count 0 should yield 0");
        CHECK(matched == 0, "matched should be 0 when count is 0");
    }

    /* No located variables at all, but globals declared — nothing to join. */
    {
        reset();
        const void *globals[] = { S(0) };
        uint32_t idx[1], matched = 99;
        CHECK(located_globals_join_ex(0, g_lv, pointer_at, globals, 1, idx, &matched) == 0,
              "no located vars should yield 0");
        CHECK(matched == 0, "no located vars means no global can match");
    }

    /* Every located var is a global (no POU declares one) — the shape that
     * happened to work under the old positional rule. */
    {
        reset();
        g_lv[0] = S(0); g_lv[1] = S(1);
        const void *globals[] = { S(0), S(1) };
        uint32_t idx[2], matched = 0;
        CHECK(located_globals_join_ex(2, g_lv, pointer_at, globals, 2, idx, &matched) == 2,
              "all-globals should yield 2");
    }

    /* Defensive: NULL accessor / NULL output must not crash and must yield 0. */
    {
        reset();
        const void *globals[] = { S(0) };
        uint32_t idx[2];
        CHECK(located_globals_join_ex(2, g_lv, NULL, globals, 1, idx, NULL) == 0,
              "NULL pointer_at should yield 0");
        CHECK(located_globals_join_ex(2, g_lv, pointer_at, globals, 1, NULL, NULL) == 0,
              "NULL out_idx should yield 0");
    }
}

void test_inconsistency_detected(void)
{
    reset();
    g_lv[0] = S(0);
    g_lv[1] = S(1);
    /* S(5) belongs to no located variable — codegen bug. */
    const void *globals[] = { S(0), S(5) };

    uint32_t idx[2], matched = 0;
    uint32_t n = located_globals_join_ex(2, g_lv, pointer_at, globals, 2,
                                         idx, &matched);
    CHECK(n == 1, "expected 1 config global, got %u", n);
    CHECK(idx[0] == 0, "expected index 0, got %u", idx[0]);
    CHECK(matched == 1, "expected matched=1 (< count 2) to flag the mismatch, got %u",
          matched);
}

void test_unbound_skipped(void)
{
    reset();
    g_lv[0] = S(0);
    g_lv[1] = NULL;   /* never bound */
    g_lv[2] = S(2);
    const void *globals[] = { S(0), S(2) };

    uint32_t idx[3], matched = 0;
    uint32_t n = located_globals_join_ex(3, g_lv, pointer_at, globals, 2,
                                         idx, &matched);
    CHECK(n == 2, "expected 2 config globals, got %u", n);
    CHECK(idx[0] == 0 && idx[1] == 2, "expected [0,2], got [%u,%u]", idx[0], idx[1]);
}

void setUp(void) { reset(); }
void tearDown(void) {}
