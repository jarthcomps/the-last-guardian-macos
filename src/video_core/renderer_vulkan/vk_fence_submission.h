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

} // namespace Vulkan
