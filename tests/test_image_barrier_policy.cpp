// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cassert>

#include "video_core/texture_cache/image_barrier_policy.h"

using VideoCore::CanSkipImageReadAfterRead;

int main() {
    constexpr std::uint64_t ShaderRead = 1 << 0;
    constexpr std::uint64_t TransferRead = 1 << 1;
    constexpr std::uint64_t ShaderWrite = 1 << 2;
    constexpr std::uint64_t ColorWrite = 1 << 3;
    constexpr std::uint64_t Writes = ShaderWrite | ColorWrite;
    static_assert(CanSkipImageReadAfterRead(ShaderRead, TransferRead, Writes));
    static_assert(CanSkipImageReadAfterRead(ShaderRead, ShaderRead, Writes));
    static_assert(CanSkipImageReadAfterRead(0, ShaderRead, Writes)); // undefined -> read
    static_assert(!CanSkipImageReadAfterRead(ColorWrite, ShaderRead, Writes)); // RAW
    static_assert(!CanSkipImageReadAfterRead(ShaderRead, ShaderWrite, Writes)); // WAR
    static_assert(!CanSkipImageReadAfterRead(ShaderRead | ShaderWrite, ShaderRead, Writes));
    assert(!VideoCore::ImageBarrierPolicy::skip_read_after_read);
    return 0;
}
