// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Autonomy®

// The live retain pack's torn-read check on strings
// (core/strucpp_runtime/retain_live_read.hpp): a value is read until two
// reads agree, at any length. It once skipped the check above 1024 bytes,
// so a long STRING written mid-read was stored half old, half new.

#include <cstdio>
#include <cstring>
#include <vector>

#include "../../core/strucpp_runtime/retain_live_read.hpp"

static int g_fail = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("  FAIL  %s (line %d)\n", msg, __LINE__);             \
            ++g_fail;                                                          \
        }                                                                      \
    } while (0)

// A string the scan rewrites while it is being read: the first `torn` reads
// see a mix of old and new, every later one the new value.
struct ChangingString {
    std::vector<uint8_t> value;
    int torn  = 0;
    int reads = 0;

    uint16_t read(uint8_t*, uint16_t, uint8_t* dest, uint16_t cap) {
        const uint16_t n = value.size() < cap ? (uint16_t)value.size() : cap;
        std::memcpy(dest, value.data(), n);
        if (reads++ < torn) dest[n / 2] ^= 0xFF;  // half old, half new
        return n;
    }
};

static void check_length(uint16_t len) {
    ChangingString s;
    s.value.resize(len);
    for (uint16_t i = 0; i < len; ++i) s.value[i] = (uint8_t)('a' + i % 26);
    s.torn = 1;

    std::vector<uint8_t> dest(len);
    auto reader = [&s](uint8_t, uint16_t, uint8_t* d, uint16_t c) { return s.read(nullptr, 0, d, c); };
    const uint16_t n = retain_live::read_text_stable(reader, 0, 0, dest.data(), len, 8);

    char msg[96];
    std::snprintf(msg, sizeof msg, "%u bytes: a torn first read is retried", (unsigned)len);
    CHECK(n == len && std::memcmp(dest.data(), s.value.data(), len) == 0, msg);
    std::snprintf(msg, sizeof msg, "%u bytes: two reads agreed", (unsigned)len);
    CHECK(s.reads >= 3, msg);
}

int main() {
    check_length(300);
    check_length(1024);
    check_length(1025);   // STRING(1024) / WSTRING(512) and up
    check_length(4000);
    check_length(65535);  // the largest retain blob
    check_length(320);    // smaller again after a larger one
    if (g_fail) {
        std::printf("test_retain_live_read: %d failed\n", g_fail);
        return 1;
    }
    std::printf("test_retain_live_read: all passed\n");
    return 0;
}
