// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <initializer_list>
#include "video_core/renderer_vulkan/vk_fence_submission.h"

int main() {
    using namespace Vulkan;
    for (bool encoder : {false, true}) {
        for (bool direct : {false, true}) {
            for (bool rendering : {false, true}) {
                for (bool empty : {false, true}) {
                    const bool reusable = CanReuseSubmittedTick(encoder, direct, rendering, empty);
                    const auto fence = PlanFenceSubmission(5, false, true, reusable, false);
                    const bool must_submit = !encoder || direct || rendering || !empty;
                    if (fence.submit != must_submit || fence.tick != (must_submit ? 5 : 4)) {
                        return 1;
                    }
                }
            }
        }
    }
    const auto copy = PlanFenceSubmission(5, true, true, true, false);
    if (!copy.submit || copy.tick != 5) return 2;
    const auto prior = PlanFenceSubmission(5, false, true, true, false);
    if (prior.submit || prior.tick != 4) return 3;
    const auto pending = PlanFenceSubmission(5, false, true, true, true);
    if (!pending.submit || pending.tick != 4) return 4;
    const auto relaxed = PlanFenceSubmission(5, false, false, false, false);
    if (relaxed.submit || relaxed.tick != 4) return 5;
    const auto initial = PlanFenceSubmission(1, false, true, true, false);
    if (initial.submit || initial.tick != 0) return 6;
    for (std::uint64_t completed = 0; completed <= 5; ++completed) {
        if ((completed >= prior.tick) != (completed >= 4)) return 7;
        if ((completed >= copy.tick) != (completed >= 5)) return 8;
    }
    // Early interrupting EOPs: only with the mode on, an interrupt, and no readback copies.
    for (bool mode : {false, true}) {
        for (bool interrupting : {false, true}) {
            for (bool copies : {false, true}) {
                if (SignalAtCommandProcessor(mode, interrupting, copies) !=
                    (mode && interrupting && !copies)) {
                    return 9;
                }
            }
        }
    }
    return 0;
}
