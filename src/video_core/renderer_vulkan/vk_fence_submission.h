// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace Vulkan {

constexpr bool CanReuseSubmittedTick(bool threaded_encoder, bool direct_recording,
                                   bool rendering, bool commands_empty) {
    return threaded_encoder && !direct_recording && !rendering && commands_empty;
}

struct FenceSubmission {
    std::uint64_t tick;
    bool submit;
};

constexpr FenceSubmission PlanFenceSubmission(std::uint64_t current_tick, bool has_copies,
                                             bool full_fence, bool reusable_tick,
                                             bool priority_needs_submit) {
    const bool current = has_copies || (full_fence && !reusable_tick);
    return {current ? current_tick : current_tick - 1, current || priority_needs_submit};
}

/// SHADPS4_EARLY_EOP=1 (experiment): an end-of-pipe fence that raises an interrupt (the one The
/// Last Guardian's render thread waits for before it submits the next frame) is signaled as soon
/// as the command processor reaches it, instead of after the GPU finished the work before it.
/// Guest memory the GPU reads was already copied when the commands were recorded, and pages the
/// GPU writes stay protected until their data is back (precise readbacks), so the next frame's
/// translation overlaps the GPU tail of this one. A fence with readback copies still waits.
/// Measured outdoors (M2 Max): slower, 20.4 against 22.5 FPS. The game reads GPU results every
/// frame after this fence, so it faulted and waited on them instead. Kept as a diagnostic.
constexpr bool SignalAtCommandProcessor(bool early_mode, bool interrupting, bool has_copies) {
    return early_mode && interrupting && !has_copies;
}

} // namespace Vulkan
