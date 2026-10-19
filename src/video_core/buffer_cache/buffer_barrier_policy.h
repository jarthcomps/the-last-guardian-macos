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

/// How a shader buffer binding is ordered under SHADPS4_PRECISE_BUFFER_HAZARDS.
enum class PreciseBarrier {
    Normal, ///< whole-buffer access state decides (precise mode off, or a draw)
    Force,  ///< an overlapping range was touched since the last full barrier: always a barrier
    Elide,  ///< nothing overlapping since the last full barrier: no barrier
};

/// `tracked` = precise mode covers this binding (compute, or draws with mode 2); `hazard` = the
/// range tracker found an overlapping access in the current barrier epoch; `external_write` = the
/// buffer's last recorded access includes a transfer, host or memory write, which the range
/// tracker does not see. A forced barrier also covers write after write and write after read,
/// which the whole-buffer state alone leaves out.
constexpr PreciseBarrier ClassifyPreciseBarrier(bool precise, bool tracked, bool hazard,
                                                bool external_write) noexcept {
    if (!precise || !tracked || external_write) {
        return PreciseBarrier::Normal;
    }
    return hazard ? PreciseBarrier::Force : PreciseBarrier::Elide;
}

/// SHADPS4_PRECISE_HAZARD_SHADERS="1d99a308,96d32d22,...": precise hazards only for compute
/// shaders whose 32-bit program hash is listed (bisecting which elisions are safe). Empty = all.
inline bool PreciseHazardShaderAllowed(const char* list, std::uint64_t pgm_hash) noexcept {
    if (list == nullptr || *list == 0) {
        return true;
    }
    const std::uint64_t low = pgm_hash & 0xffffffffULL;
    std::uint64_t value = 0;
    int digits = 0;
    for (const char* c = list;; ++c) {
        const char ch = *c;
        int v = -1;
        if (ch >= '0' && ch <= '9') v = ch - '0';
        else if (ch >= 'a' && ch <= 'f') v = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') v = ch - 'A' + 10;
        if (v >= 0) {
            value = (value << 4) | static_cast<std::uint64_t>(v);
            ++digits;
            continue;
        }
        if (digits > 0 && (value & 0xffffffffULL) == low) {
            return true;
        }
        value = 0;
        digits = 0;
        if (ch == 0) {
            return false;
        }
    }
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
