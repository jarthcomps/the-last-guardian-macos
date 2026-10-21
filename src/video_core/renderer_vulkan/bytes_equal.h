// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstring>

#include "common/types.h"

namespace Vulkan {

/// True when the `size` bytes at `a` and `b` are equal, the same answer as memcmp. The blocks
/// compared on the command thread are small and their sizes are known at each call site, so the
/// words are loaded with memcpy and compared inline. That is faster than a memcmp call for these
/// sizes on the x86-64 build (run under Rosetta).
inline bool BytesEqual(const u8* a, const u8* b, std::size_t size) noexcept {
    std::size_t i = 0;
    for (; i + sizeof(u64) <= size; i += sizeof(u64)) {
        u64 wa, wb;
        std::memcpy(&wa, a + i, sizeof(u64));
        std::memcpy(&wb, b + i, sizeof(u64));
        if (wa != wb) {
            return false;
        }
    }
    if (i + sizeof(u32) <= size) {
        u32 wa, wb;
        std::memcpy(&wa, a + i, sizeof(u32));
        std::memcpy(&wb, b + i, sizeof(u32));
        if (wa != wb) {
            return false;
        }
        i += sizeof(u32);
    }
    for (; i < size; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

} // namespace Vulkan
