// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <cstdint>

namespace VideoCore {

/// SHADPS4_SKIP_IMAGE_READ_READ_BARRIERS=1: a whole-image transition from one read-only use to
/// another (sampled, then copied from, for example) is not recorded. Two reads need no ordering,
/// and KosmicKrisp ignores image layouts, so on Metal such a barrier only serializes the work
/// around it. The tracked state then becomes the union of both uses, so the next write waits for
/// every reader. Masks are plain integers so the rule can be unit tested without Vulkan headers.
constexpr bool CanSkipImageReadAfterRead(std::uint64_t prev_access, std::uint64_t dst_access,
                                         std::uint64_t write_bits) noexcept {
    return (prev_access & write_bits) == 0 && (dst_access & write_bits) == 0;
}

struct ImageBarrierPolicy {
    /// SHADPS4_SKIP_IMAGE_READ_READ_BARRIERS=1, read once when the texture cache starts.
    static inline bool skip_read_after_read = false;
    /// Transitions skipped by that rule (diagnostics).
    static inline std::atomic<std::uint64_t> read_after_read_skipped{0};
};

} // namespace VideoCore
