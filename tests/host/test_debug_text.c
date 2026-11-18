// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Autonomy®

/*
 * FC 0x4F DEBUG_GET_TEXT (debug_handler.c) against a stand-in for the
 * program's strucpp_debug_read_text:
 *   - a STRING(320) holding 300 characters reads whole, and from an offset;
 *   - a WSTRING longer than the frame is clamped to the 4096-byte frame;
 *   - a program without the symbol, or no program, answers 0x83;
 *   - a short request or an array out of range answers 0x81.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "debug_handler.h"
#include "image_tables.h"
#include "utils/utils.h"

/* ---- stand-ins for what debug_handler.c links against ------------------ */

unsigned long scan_counter = 0;
char *ext_strucpp_program_md5 = NULL;

uint8_t (*ext_strucpp_debug_array_count)(void)                   = NULL;
uint16_t (*ext_strucpp_debug_elem_count)(uint8_t)                = NULL;
uint16_t (*ext_strucpp_debug_size)(uint8_t, uint16_t)            = NULL;
uint8_t (*ext_strucpp_debug_set)(uint8_t, uint16_t, bool, const uint8_t *, uint16_t) = NULL;
uint16_t (*ext_strucpp_debug_read)(uint8_t, uint16_t, uint8_t *) = NULL;
plc_debug_read_text_func_t ext_strucpp_debug_read_text           = NULL;

int runtime_external_write(uint8_t arr, uint16_t elem, uint8_t op, const uint8_t *bytes,
                           uint16_t len)
{
    return 0;
}

void log_error(const char *fmt, ...) { (void)fmt; }
void log_debug(const char *fmt, ...) { (void)fmt; }

/* ---- the program: arr 0, elem 0 = STRING(320), elem 1 = WSTRING(3000), elem 2 = INT */

#define S_CAP 320
#define W_CAP 3000
static uint8_t  s_chars[S_CAP];
static uint16_t s_len;
static uint16_t w_units[W_CAP];
static uint16_t w_len;
static uint16_t g_last_max, g_last_data_cap;

static uint8_t stub_array_count(void) { return 1; }
static uint16_t stub_elem_count(uint8_t arr) { return arr == 0 ? 3 : 0; }
static uint16_t stub_size(uint8_t arr, uint16_t elem) { return 2; }
static uint8_t stub_set(uint8_t a, uint16_t e, bool f, const uint8_t *b, uint16_t l) { return 0x7E; }
static uint16_t stub_read(uint8_t arr, uint16_t elem, uint8_t *dest) { return 0; }

/* Interface 1 semantics: copy up to `max` units from `offset`, never past
 * data_cap bytes. */
static uint8_t stub_read_text(uint8_t arr, uint16_t elem, uint16_t offset, uint16_t max,
                              uint8_t *data, uint16_t data_cap, plc_text_chunk_t *info)
{
    g_last_max      = max;
    g_last_data_cap = data_cap;
    memset(info, 0, sizeof(*info));
    if (arr != 0 || elem > 1)
    {
        info->status = 0x81;
        return info->status;
    }
    const int      wide  = elem == 1;
    const uint16_t len   = wide ? w_len : s_len;
    const uint16_t unit  = wide ? 2 : 1;
    uint16_t       count = offset < len ? (uint16_t)(len - offset) : 0;
    if (count > max) count = max;
    if (count > data_cap / unit) count = (uint16_t)(data_cap / unit);
    for (uint16_t i = 0; i < count; ++i)
    {
        if (wide)
        {
            data[2 * i]     = (uint8_t)(w_units[offset + i] & 0xFF);
            data[2 * i + 1] = (uint8_t)(w_units[offset + i] >> 8);
        }
        else
        {
            data[i] = s_chars[offset + i];
        }
    }
    info->status = 0x7E;
    info->tag    = wide ? PLC_TEXT_TAG_WSTRING : PLC_TEXT_TAG_STRING;
    info->flags  = wide ? 1 : 0;
    info->cap    = wide ? W_CAP : S_CAP;
    info->len    = len;
    info->count  = count;
    return info->status;
}

static void load_program(bool with_text)
{
    ext_strucpp_debug_array_count = stub_array_count;
    ext_strucpp_debug_elem_count  = stub_elem_count;
    ext_strucpp_debug_size        = stub_size;
    ext_strucpp_debug_set         = stub_set;
    ext_strucpp_debug_read        = stub_read;
    ext_strucpp_debug_read_text   = with_text ? stub_read_text : NULL;
}

static void unload_program(void)
{
    ext_strucpp_debug_array_count = NULL;
    ext_strucpp_debug_elem_count  = NULL;
    ext_strucpp_debug_size        = NULL;
    ext_strucpp_debug_set         = NULL;
    ext_strucpp_debug_read        = NULL;
    ext_strucpp_debug_read_text   = NULL;
}

/* ---- harness ------------------------------------------------------------ */

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

static uint8_t frame[4096];

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static size_t request(uint8_t arr, uint16_t elem, uint16_t offset, uint16_t max, size_t len)
{
    memset(frame, 0xCC, sizeof(frame));
    frame[0] = 0x4F;
    frame[1] = arr;
    frame[2] = (uint8_t)(elem >> 8);
    frame[3] = (uint8_t)elem;
    frame[4] = (uint8_t)(offset >> 8);
    frame[5] = (uint8_t)offset;
    frame[6] = (uint8_t)(max >> 8);
    frame[7] = (uint8_t)max;
    return process_debug_data(frame, len);
}

#define HDR 16

static void case_string_320(void)
{
    g_case = "STRING(320) holding 300 characters";
    load_program(true);
    s_len = 300;
    for (int i = 0; i < S_CAP; ++i) s_chars[i] = (uint8_t)('A' + i % 26);
    scan_counter = 0x01020304;

    size_t n = request(0, 0, 0, 1000, 8);
    CHECK(n == HDR + 300, "whole value in one chunk");
    CHECK(frame[0] == 0x4F && frame[1] == 0x7E, "FC and OK");
    CHECK(frame[2] == PLC_TEXT_TAG_STRING && frame[3] == 0, "tag and flags");
    CHECK(be16(&frame[4]) == 320, "cap");
    CHECK(be16(&frame[6]) == 300, "len");
    CHECK(be16(&frame[8]) == 0, "offset");
    CHECK(be16(&frame[10]) == 300, "count");
    CHECK(be32(&frame[12]) == 0x01020304, "tick");
    CHECK(memcmp(&frame[HDR], s_chars, 300) == 0, "data");

    g_case = "STRING(320) from offset 290";
    n = request(0, 0, 290, 50, 8);
    CHECK(n == HDR + 10, "the last ten characters");
    CHECK(be16(&frame[8]) == 290 && be16(&frame[10]) == 10, "offset and count");
    CHECK(memcmp(&frame[HDR], &s_chars[290], 10) == 0, "data");

    g_case = "STRING(320), max 0";
    n = request(0, 0, 0, 0, 8);
    CHECK(n == HDR && frame[1] == 0x7E && be16(&frame[6]) == 300, "header only, len known");
}

static void case_frame_clamp(void)
{
    g_case = "WSTRING(3000) is clamped to the 4096-byte frame";
    load_program(true);
    w_len = 3000;
    for (int i = 0; i < W_CAP; ++i) w_units[i] = (uint16_t)(0x4100 + i);

    size_t n = request(0, 1, 0, 0xFFFF, 8);
    CHECK(g_last_data_cap == 4096 - HDR, "data_cap is the frame minus the header");
    CHECK(g_last_max <= 4096 - HDR, "max clamped to the frame");
    CHECK(be16(&frame[10]) == (4096 - HDR) / 2, "2040 units");
    CHECK(n == (size_t)(HDR + 2 * be16(&frame[10])), "length matches count");
    CHECK(n <= 4096, "never past the frame");
    CHECK(frame[2] == PLC_TEXT_TAG_WSTRING && frame[3] == 1, "tag and forced flag");
    CHECK(frame[HDR] == 0x00 && frame[HDR + 1] == 0x41, "little-endian units");

    g_case = "WSTRING(3000), second chunk";
    n = request(0, 1, 2040, 0xFFFF, 8);
    CHECK(be16(&frame[10]) == 960 && n == HDR + 1920, "the rest");
}

static void case_errors(void)
{
    g_case = "program without strucpp_debug_read_text";
    load_program(false);
    size_t n = request(0, 0, 0, 100, 8);
    CHECK(n == HDR, "full header");
    CHECK(frame[0] == 0x4F && frame[1] == 0x83, "0x83 not loaded");
    CHECK(be16(&frame[10]) == 0, "no data");

    g_case = "no program loaded";
    unload_program();
    n = request(0, 0, 0, 100, 8);
    CHECK(n == HDR && frame[1] == 0x83, "0x83 not loaded");

    g_case = "short request";
    load_program(true);
    n = request(0, 0, 0, 100, 7);
    CHECK(n == HDR && frame[1] == 0x81, "0x81");

    g_case = "array out of range";
    n = request(5, 0, 0, 100, 8);
    CHECK(n == HDR && frame[1] == 0x81, "0x81");

    g_case = "not a string";
    n = request(0, 2, 0, 100, 8);
    CHECK(n == HDR && frame[1] == 0x81 && be16(&frame[10]) == 0, "status passed through");
}

int main(void)
{
    case_string_320();
    case_frame_clamp();
    case_errors();
    if (g_fail)
    {
        printf("test_debug_text: %d failed\n", g_fail);
        return 1;
    }
    printf("test_debug_text: all passed\n");
    return 0;
}
