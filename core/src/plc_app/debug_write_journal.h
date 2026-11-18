// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Autonomy®

/* Serialized external write/force path. Writers ENQUEUE from any
 * thread via runtime_external_write; the dispatcher drains at the
 * no-task-running window, so IECVars are never touched concurrently. */
#ifndef DEBUG_WRITE_JOURNAL_H
#define DEBUG_WRITE_JOURNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* External-write operation kinds. */
typedef enum {
    DBGW_OP_WRITE   = 0, /* soft write — set value_ (respects an active force) */
    DBGW_OP_FORCE   = 1, /* force — pin value_ to the payload                  */
    DBGW_OP_UNFORCE = 2, /* unforce — release a pinned variable                */
    DBGW_OP_TEXT    = 3  /* full-length STRING/WSTRING write (soft)             */
} debug_write_op_t;

/* Enqueue an external write/force/unforce of debug leaf (arr, elem).
 * Thread-safe. bytes/len are the payload for WRITE/FORCE (ignored for
 * UNFORCE). Returns 0, or -1 if full (dropped + logged). Applied at
 * the next dispatcher drain. */
int runtime_external_write(uint8_t arr, uint16_t elem, uint8_t op,
                           const uint8_t *bytes, uint16_t len);

/* Enqueue a full-length STRING/WSTRING write: `count` code units in `nbytes`
 * bytes (count, or 2 * count for WSTRING), copied to the heap and applied by
 * the drain through strucpp_debug_write_text. Returns 0, or -1 if the queue is
 * full, memory runs out, or the program lacks the export. */
int runtime_external_write_text(uint8_t arr, uint16_t elem, const uint8_t *data,
                                uint16_t count, size_t nbytes);

/* The same write as the plugins ask for it: `count` code units, the width and
 * declared length taken from the leaf. Cut to the declared length; a CONSTANT
 * refused before queueing. Returns 0x7E queued, 0x81 not a string or out of
 * range, 0x82 queue full or out of memory, 0x83 the program lacks the
 * exports, 0x87 read-only. */
uint8_t runtime_external_write_text_units(uint8_t arr, uint16_t elem, const uint8_t *data,
                                          uint16_t count);

/* Immediate write with the drain's routing; refused (-1) outside an open direct window. */
int runtime_external_write_now(uint8_t arr, uint16_t elem, uint8_t op,
                               const uint8_t *bytes, uint16_t len);

/* Open/close the direct window. Only while no IEC task can run and after journal_init(). */
void debug_write_journal_set_direct_window(bool open);

/*
 * Drain + apply all queued external writes. MUST be called only by the
 * dispatcher at the no-task-running window, with the image lock held (the
 * located path commits through the image). Cheap no-op when the queue is
 * empty.
 */
void debug_write_journal_drain(void);

/* Reset the queue (called on program unload/stop). */
void debug_write_journal_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_WRITE_JOURNAL_H */
