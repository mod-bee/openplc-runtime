// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Autonomy®

/**
 * Debug reads of program variables, for FC 0x43/0x44/0x4F and the plugins'
 * debug_read / debug_read_text, from any thread other than a PLC task's.
 *
 * A global is read under its own lock (strucpp_global_try_lock, polled for at
 * most DEBUG_READ_GLOBAL_WAIT_US, so a PLC task is never held up waiting for a
 * reader). A task-local variable, or a global whose lock did not come, is read
 * until two reads agree. A program without the lock exports is read once.
 */

#ifndef DEBUG_READ_GUARD_H
#define DEBUG_READ_GUARD_H

#include <stdint.h>

#include "debug_text.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* How long a reader polls a global's lock before reading it as it stands. */
#define DEBUG_READ_GLOBAL_WAIT_US 2000
/* Reads of a value before the last one is taken as it is. */
#define DEBUG_READ_TRIES 8

    /** ext_strucpp_debug_read, guarded as above. Returns its byte count. */
    uint16_t debug_read_guarded(uint8_t arr, uint16_t elem, uint8_t *dest);

    /** ext_strucpp_debug_read_text, guarded as above, per chunk. */
    uint8_t debug_read_text_guarded(uint8_t arr, uint16_t elem, uint16_t offset, uint16_t max,
                                    uint8_t *data, uint16_t data_cap, plc_text_chunk_t *info);

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_READ_GUARD_H */
