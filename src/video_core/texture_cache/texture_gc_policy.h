// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace VideoCore {

/// How a texture cache garbage collection pass runs.
struct ImageGcPlan {
    bool run = false;        ///< collect at all
    bool pressured = false;  ///< also evict images the GPU wrote (downloading them first)
    bool aggressive = false; ///< evict younger images, more per pass
};

/// The memory thresholds come from the Vulkan device budget, which on a unified-memory Mac is
/// most of RAM, so outdoors in The Last Guardian collection never started (trigger 8.7 GB, usage
/// 3.5 GB) while the game created about 20 small images a second at new addresses (4x4 R32F,
/// per-frame scratch) that were never freed: 7,000 images after five minutes (about 900 MB of
/// images in all, flat). In alternating 5-minute runs the leak did not change the frame rate
/// (with 23.5/23.7 FPS, without 21.4/24.5), so this stays off. SHADPS4_TEXTURE_GC_IMAGES=N
/// (`image_limit`) also
/// collects, as under memory pressure, once N images are registered, and aggressively from 2N.
/// 0 keeps the memory thresholds alone.
constexpr ImageGcPlan PlanImageGc(std::uint64_t used, std::uint64_t trigger,
                                  std::uint64_t pressure, std::uint64_t critical,
                                  std::uint64_t image_count, std::uint64_t image_limit) {
    const bool over_count = image_limit != 0 && image_count >= image_limit;
    const bool far_over_count = image_limit != 0 && image_count >= 2 * image_limit;
    return {
        .run = used >= trigger || over_count,
        .pressured = used >= pressure || over_count,
        .aggressive = used >= critical || far_over_count,
    };
}

/// Whether a reused render target must touch its image now. The collector only compares an
/// image's last touch with its own tick, so once per tick is enough (`once`); otherwise every
/// reuse touches, as before.
constexpr bool NeedsTargetTouch(bool once, std::uint64_t touched_tick, std::uint64_t gc_tick) {
    return !once || touched_tick != gc_tick;
}

} // namespace VideoCore
