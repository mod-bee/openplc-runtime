// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Autonomy®

/**
 * Full-length STRING/WSTRING access (FC 0x4F reads, plugin writes), the runtime side.
 *
 * C mirror of STruC++'s `strucpp_text_chunk` (debug_dispatch.hpp), which the
 * runtime cannot include. Same field order and widths; the shim
 * (runtime_v4_entry.cpp) static_asserts the STruC++ struct to the same layout
 * that is asserted below.
 */

#ifndef DEBUG_TEXT_H
#define DEBUG_TEXT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        uint8_t  status; /* 0x7E OK, 0x81 not a string / out of range, 0x83 not loaded */
        uint8_t  tag;    /* debug type tag of the leaf (STRING or WSTRING) */
        uint8_t  flags;  /* PLC_TEXT_FLAG_* */
        uint16_t cap;    /* declared length, code units */
        uint16_t len;    /* current length, code units */
        uint16_t count;  /* code units copied into data */
    } plc_text_chunk_t;

    /** strucpp_debug_read_text(): copies up to `max` code units from code unit
     *  `offset` into `data` (STRING bytes, WSTRING little-endian units), never
     *  more than `data_cap` bytes. Returns info->status. */
    typedef uint8_t (*plc_debug_read_text_func_t)(uint8_t arr, uint16_t elem, uint16_t offset,
                                                  uint16_t max, uint8_t *data, uint16_t data_cap,
                                                  plc_text_chunk_t *info);

    /** strucpp_debug_write_text(): sets (does not force) a STRING/WSTRING leaf to
     *  `count` code units from `data`, cut to the declared length. Returns 0x7E,
     *  0x81 not a string or out of range, 0x83 not loaded. */
    typedef uint8_t (*plc_debug_write_text_func_t)(uint8_t arr, uint16_t elem,
                                                   const uint8_t *data, uint16_t count);

#ifdef __cplusplus
}
#define PLC_TEXT_STATIC_ASSERT static_assert
#else
#define PLC_TEXT_STATIC_ASSERT _Static_assert
#endif

/* plc_text_chunk_t.flags */
#define PLC_TEXT_FLAG_FORCED    0x01
#define PLC_TEXT_FLAG_READ_ONLY 0x02 /* a CONSTANT: writes are refused with 0x87 */

/* strucpp::debug::TAG_STRING / TAG_WSTRING; the shim asserts them. */
#define PLC_TEXT_TAG_STRING  19
#define PLC_TEXT_TAG_WSTRING 20

#define PLC_TEXT_CHUNK_SIZE         10
#define PLC_TEXT_CHUNK_OFFSET_CAP   4
#define PLC_TEXT_CHUNK_OFFSET_LEN   6
#define PLC_TEXT_CHUNK_OFFSET_COUNT 8

PLC_TEXT_STATIC_ASSERT(sizeof(plc_text_chunk_t) == PLC_TEXT_CHUNK_SIZE, "chunk size");
PLC_TEXT_STATIC_ASSERT(offsetof(plc_text_chunk_t, cap) == PLC_TEXT_CHUNK_OFFSET_CAP, "cap offset");
PLC_TEXT_STATIC_ASSERT(offsetof(plc_text_chunk_t, len) == PLC_TEXT_CHUNK_OFFSET_LEN, "len offset");
PLC_TEXT_STATIC_ASSERT(offsetof(plc_text_chunk_t, count) == PLC_TEXT_CHUNK_OFFSET_COUNT,
                       "count offset");

#endif /* DEBUG_TEXT_H */
