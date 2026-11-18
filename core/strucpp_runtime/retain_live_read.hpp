// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Autonomy®

// Torn-read check for strucpp_retain_pack2_live (runtime_v4_entry.cpp): a
// STRING/WSTRING is read until two reads agree. Header-only and free of
// STruC++ types so tests/host can drive it with a stand-in reader.

#ifndef RETAIN_LIVE_READ_HPP
#define RETAIN_LIVE_READ_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace retain_live {

/** read_text(arr, elem, dest, cap_bytes) -> bytes, at whatever widths the
 *  reader uses (byte counts are never narrowed here). Retries up to `tries`
 *  times until two consecutive reads agree; returns the last read's length.
 *  The compare buffer grows to the longest string seen, per thread (the retain
 *  store's, never a scan). Only an allocation failure skips the check. */
template <typename ReadText, typename Bytes>
auto read_text_stable(ReadText read_text, uint8_t arr, uint16_t elem, uint8_t* dest, Bytes cap,
                      int tries) -> decltype(read_text(arr, elem, dest, cap)) {
    static thread_local std::vector<uint8_t> again;
    bool compare = true;
    if (again.size() < static_cast<size_t>(cap)) {
        try {
            again.resize(static_cast<size_t>(cap));
        } catch (...) {
            compare = false;
        }
    }
    decltype(read_text(arr, elem, dest, cap)) n = 0;
    for (int t = 0; t < tries; ++t) {
        n = read_text(arr, elem, dest, cap);
        if (!compare) return n;
        const auto m = read_text(arr, elem, again.data(), cap);
        if (m == n && std::memcmp(dest, again.data(), n) == 0) return n;
    }
    return n;
}

}  // namespace retain_live

#endif  // RETAIN_LIVE_READ_HPP
