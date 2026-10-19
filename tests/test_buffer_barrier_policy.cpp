// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cassert>

#include "video_core/buffer_cache/buffer_barrier_policy.h"

using VideoCore::NeedsBufferBarrier;

int main() {
    constexpr std::uint64_t read = 1u << 0;
    constexpr std::uint64_t write = 1u << 1;
    constexpr std::uint64_t other_read = 1u << 2;
    constexpr std::uint64_t write_bits = write;
    constexpr std::uint64_t compute = 1u << 4;
    constexpr std::uint64_t all = 1u << 5;

    // Upstream behaviour (rule off).
    static_assert(!NeedsBufferBarrier(read, all, read, all, write_bits, false), "read after read");
    static_assert(!NeedsBufferBarrier(write, all, write, all, write_bits, false),
                  "upstream skips write after write");
    static_assert(NeedsBufferBarrier(write, all, read, all, write_bits, false), "read after write");
    static_assert(NeedsBufferBarrier(read, all, write, all, write_bits, false), "write after read");
    static_assert(NeedsBufferBarrier(read, all, other_read, all, write_bits, false),
                  "different read kind");
    static_assert(NeedsBufferBarrier(read, compute, read, all, write_bits, false),
                  "different stage");

    // Rule on: only same-mask writes change.
    static_assert(NeedsBufferBarrier(write, all, write, all, write_bits, true),
                  "write after write gets a barrier");
    static_assert(NeedsBufferBarrier(write | read, all, write | read, all, write_bits, true),
                  "read-write after read-write gets a barrier");
    static_assert(!NeedsBufferBarrier(read, all, read, all, write_bits, true),
                  "read after read still free");
    static_assert(NeedsBufferBarrier(write, all, read, all, write_bits, true));
    static_assert(NeedsBufferBarrier(read, all, write, all, write_bits, true));

    // Read-after-read skipping.
    static_assert(!NeedsBufferBarrier(read, all, other_read, compute, write_bits, false, true),
                  "read then another kind of read: no barrier");
    static_assert(NeedsBufferBarrier(read, all, other_read, compute, write_bits, false, false),
                  "upstream keeps it");
    static_assert(NeedsBufferBarrier(write, all, read, all, write_bits, false, true),
                  "read after write still synchronizes");
    static_assert(NeedsBufferBarrier(read, all, write, all, write_bits, false, true),
                  "write after read still synchronizes");
    static_assert(!NeedsBufferBarrier(read, all, read, all, write_bits, false, true));

    // The counter and switch are plain globals.
    assert(!VideoCore::BufferBarrierPolicy::write_after_write);
    VideoCore::BufferBarrierPolicy::write_after_write_barriers.fetch_add(2);
    assert(VideoCore::BufferBarrierPolicy::write_after_write_barriers.exchange(0) == 2);
    // Precise hazards classification.
    {
        using VideoCore::ClassifyPreciseBarrier;
        using VideoCore::PreciseBarrier;
        if (ClassifyPreciseBarrier(false, true, true, false) != PreciseBarrier::Normal) return 90;
        if (ClassifyPreciseBarrier(true, false, false, false) != PreciseBarrier::Normal) return 91;
        if (ClassifyPreciseBarrier(true, true, false, true) != PreciseBarrier::Normal) return 92;
        if (ClassifyPreciseBarrier(true, true, true, false) != PreciseBarrier::Force) return 93;
        if (ClassifyPreciseBarrier(true, true, false, false) != PreciseBarrier::Elide) return 94;
    }
    // Precise hazard shader allowlist.
    {
        using VideoCore::PreciseHazardShaderAllowed;
        if (!PreciseHazardShaderAllowed(nullptr, 0x1234)) return 95;
        if (!PreciseHazardShaderAllowed("", 0x1234)) return 96;
        if (!PreciseHazardShaderAllowed("1d99a308,96d32d22", 0x1d99a308)) return 97;
        if (!PreciseHazardShaderAllowed("1d99a308,96d32d22", 0x96d32d22)) return 98;
        if (PreciseHazardShaderAllowed("1d99a308,96d32d22", 0x12345678)) return 99;
        if (!PreciseHazardShaderAllowed("000000001D99A308", 0x1d99a308)) return 100;
    }
    return 0;
}
