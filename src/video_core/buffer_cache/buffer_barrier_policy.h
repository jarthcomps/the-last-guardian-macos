// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <cstdint>

namespace VideoCore {

/// Decides when a buffer binding needs a pipeline barrier.
///
/// Upstream skips the barrier whenever a buffer is used again with the same access mask and
/// stage. For two consecutive shader writes (a dispatch writing a buffer the previous dispatch
/// also wrote) that leaves no ordering between them. AMD hardware and most PC drivers happen to
/// run such dispatches in order, but a Metal 4 compute encoder runs dispatches concurrently unless
/// a barrier separates them, so the second dispatch can read half-written data from the first
/// (suspected cause of Trico's flickering feathers on macOS). With `write_after_write` enabled
/// (SHADPS4_WRITE_AFTER_WRITE_BARRIERS=1) a write following a write gets a barrier as well.
///
/// Masks are plain integers so the rule can be unit tested without Vulkan headers.
constexpr bool NeedsBufferBarrier(std::uint64_t prev_access, std::uint64_t prev_stage,
                                  std::uint64_t dst_access, std::uint64_t dst_stage,
                                  std::uint64_t write_bits, bool write_after_write,
                                  bool skip_read_after_read = false) noexcept {
    // SHADPS4_SKIP_READ_READ_BARRIERS=1: a read following a read needs no ordering at all, even
    // when the access kind or stage differs (vertex fetch then shader read, for example). On
    // KosmicKrisp every barrier inside a compute encoder serializes all dispatches around it.
    if (skip_read_after_read && (prev_access & write_bits) == 0 && (dst_access & write_bits) == 0) {
        return false;
    }
    if (prev_access != dst_access || prev_stage != dst_stage) {
        return true;
    }
    return write_after_write && (prev_access & write_bits) != 0;
}

struct BufferBarrierPolicy {
    /// SHADPS4_WRITE_AFTER_WRITE_BARRIERS=1, read once at startup.
    static inline bool write_after_write = false;
    /// Barriers added by the write-after-write rule (diagnostics, any thread).
    static inline std::atomic<std::uint64_t> write_after_write_barriers{0};
    /// SHADPS4_SKIP_READ_READ_BARRIERS=1, read once at startup.
    static inline bool skip_read_after_read = false;
    /// Barriers skipped by that rule (diagnostics).
    static inline std::atomic<std::uint64_t> read_after_read_skipped{0};
    /// SHADPS4_PRECISE_BUFFER_HAZARDS=1 (Metal): dispatches get buffer barriers only for byte
    /// ranges accessed since the last full barrier (BufferHazardTracker).
    static inline bool precise_hazards = false;
    /// SHADPS4_PRECISE_BUFFER_HAZARDS=2: draws too (a draw's buffer barrier ends the pass).
    static inline bool precise_draw_hazards = false;
    /// Dispatch buffer barriers the precise rule left out (diagnostics).
    static inline std::atomic<std::uint64_t> precise_hazards_elided{0};
};

} // namespace VideoCore
