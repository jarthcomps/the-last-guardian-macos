// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/texture_cache/texture_gc_policy.h"

int main() {
    using VideoCore::PlanImageGc;
    constexpr std::uint64_t MB = 1ULL << 20;
    // Memory thresholds alone (limit 0): the old behaviour.
    auto p = PlanImageGc(3500 * MB, 8683 * MB, 20643 * MB, 23920 * MB, 7000, 0);
    if (p.run || p.pressured || p.aggressive) return 1;
    p = PlanImageGc(9000 * MB, 8683 * MB, 20643 * MB, 23920 * MB, 10, 0);
    if (!p.run || p.pressured || p.aggressive) return 2;
    p = PlanImageGc(21000 * MB, 8683 * MB, 20643 * MB, 23920 * MB, 10, 0);
    if (!p.run || !p.pressured || p.aggressive) return 3;
    p = PlanImageGc(24000 * MB, 8683 * MB, 20643 * MB, 23920 * MB, 10, 0);
    if (!p.run || !p.pressured || !p.aggressive) return 4;
    // Image count limit: below, at, and twice the limit.
    p = PlanImageGc(3500 * MB, 8683 * MB, 20643 * MB, 23920 * MB, 1499, 1500);
    if (p.run || p.pressured || p.aggressive) return 5;
    p = PlanImageGc(3500 * MB, 8683 * MB, 20643 * MB, 23920 * MB, 1500, 1500);
    if (!p.run || !p.pressured || p.aggressive) return 6;
    p = PlanImageGc(3500 * MB, 8683 * MB, 20643 * MB, 23920 * MB, 3000, 1500);
    if (!p.run || !p.pressured || !p.aggressive) return 7;
    static_assert(PlanImageGc(0, 1, 2, 3, 5, 5).run);
    // Render target touches: every reuse without the switch, once per GC tick with it.
    using VideoCore::NeedsTargetTouch;
    if (!NeedsTargetTouch(false, 7, 7) || !NeedsTargetTouch(false, 6, 7)) return 8;
    if (NeedsTargetTouch(true, 7, 7)) return 9;
    if (!NeedsTargetTouch(true, 6, 7) || !NeedsTargetTouch(true, ~0ULL, 0)) return 10;
    return 0;
}
