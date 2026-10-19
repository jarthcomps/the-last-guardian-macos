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

/// SHADPS4_FENCE_SUBMIT_BATCH=N: a guest fence normally submits the command buffer holding the
/// work before it, so the fence can be signaled once the GPU finishes that work. Outdoors The
/// Last Guardian writes about 180 such fences per frame, and every submission costs the command
/// processor, the encoder and the GPU a command buffer. With N > 1, up to N - 1 fences in a row
/// that do not raise an interrupt and carry no readback copies leave the command buffer open; the
/// fence is still signaled after the GPU finished the work before it (only later, together with
/// the work recorded after it). The command processor submits whatever is pending before it
/// waits for anything (no guest work, a register/memory wait), so nobody waits on a fence whose
/// work was never submitted. `pending` is the number of fences already held back in the command
/// buffer being recorded; `kinds` is a FenceKind mask (SHADPS4_FENCE_BATCH_KINDS).
constexpr bool HoldFenceSubmit(std::uint32_t batch, std::uint32_t pending, bool interrupting,
                               bool has_copies, std::uint32_t kind, std::uint32_t kinds) {
    return batch > 1 && !interrupting && !has_copies && (kind & kinds) != 0 &&
           pending + 1 < batch;
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
