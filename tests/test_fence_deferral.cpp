// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cassert>
#include <string>
#include <vector>

#include "video_core/renderer_vulkan/fence_deferral.h"

using Vulkan::DeferredFenceQueue;
using Vulkan::ShouldDeferFence;

int main() {
    // enabled, async, rendering, must_sync, submit_end, downloads_attachment
    static_assert(ShouldDeferFence(true, true, true, false, false, false));
    static_assert(!ShouldDeferFence(false, true, true, false, false, false), "opt in");
    static_assert(!ShouldDeferFence(true, false, true, false, false, false), "sync fences path");
    static_assert(!ShouldDeferFence(true, true, false, false, false, false), "no open pass");
    static_assert(!ShouldDeferFence(true, true, true, true, false, false), "must_sync");
    static_assert(!ShouldDeferFence(true, true, true, false, true, false), "end of submission");
    static_assert(!ShouldDeferFence(true, true, true, false, false, true),
                  "readback of a pass attachment");

    static_assert(Vulkan::ShouldSkipCommandStreamBarrier(true, true, false));
    static_assert(!Vulkan::ShouldSkipCommandStreamBarrier(false, true, false), "opt in");
    static_assert(!Vulkan::ShouldSkipCommandStreamBarrier(true, false, false), "no open pass");
    static_assert(!Vulkan::ShouldSkipCommandStreamBarrier(true, true, true),
                  "a draw of the pass wrote memory");

    static_assert(Vulkan::ShouldRecordGuestComputeBarrier(true, false, true));
    static_assert(!Vulkan::ShouldRecordGuestComputeBarrier(false, false, true), "opt in");
    static_assert(!Vulkan::ShouldRecordGuestComputeBarrier(true, true, true), "inside a pass");
    static_assert(!Vulkan::ShouldRecordGuestComputeBarrier(true, false, false), "nothing new");

    DeferredFenceQueue queue;
    assert(queue.Empty());
    assert(queue.Take().empty());

    std::vector<int> order;
    queue.Push([&] { order.push_back(1); }, 2);
    queue.Push({}, 4);
    queue.Push([&] { order.push_back(3); }, 1);
    assert(!queue.Empty());
    assert(queue.Size() == 3);

    auto fences = queue.Take();
    assert(queue.Empty());
    assert(queue.Size() == 0);
    assert(fences.size() == 3);
    assert(fences[0].kind == 2 && fences[1].kind == 4 && fences[2].kind == 1);
    assert(fences[0].signal && !fences[1].signal && fences[2].signal);
    for (auto& fence : fences) {
        if (fence.signal) {
            fence.signal();
        }
    }
    assert((order == std::vector<int>{1, 3}));

    // Taking again yields nothing and pushes after a take start a new batch.
    assert(queue.Take().empty());
    queue.Push({}, 8);
    assert(queue.Size() == 1);

    Vulkan::DeferralStats stats;
    stats.fences = 10;
    stats.in_pass = 7;
    stats.deferred = 6;
    stats.kept_attachment = 1;
    stats.barriers_skipped = 9;
    stats.barriers_kept_memory_writes = 2;
    stats.cp_syncs_skipped = 5;
    stats.dma_copies_in_pass = 7;
    stats.dma_fills_in_pass = 0;
    stats.draw_barriers_skipped = 81;
    stats.write_after_write_barriers = 12;
    stats.read_after_read_skipped = 4;
    stats.image_read_after_read_skipped = 6;
    stats.precise_hazards_elided = 8;
    stats.guest_cs_flushes = 30;
    stats.guest_cs_flush_barriers = 25;
    stats.flushes[static_cast<size_t>(Vulkan::FlushReason::Dispatch)] = 3;
    stats.flushes[static_cast<size_t>(Vulkan::FlushReason::Yield)] = 2;
    const std::string line = stats.Format(1.0);
    assert(line.starts_with("DEFER_STATS dt=1.0"));
    assert(line.find(" fences=10 in_pass=7 deferred=6 kept_attachment=1 barriers_skipped=9 "
                     "barriers_kept_memory_writes=2 cp_syncs_skipped=5 dma_copies_in_pass=7 "
                     "dma_fills_in_pass=0 draw_barriers_skipped=81 write_after_write_barriers=12 "
                     "read_after_read_skipped=4 image_read_after_read_skipped=6 precise_hazards_elided=8 "
                     "guest_cs_flushes=30 guest_cs_flush_barriers=25 flushes:") !=
           std::string::npos);
    assert(line.find(" dispatch=3 ") != std::string::npos);
    assert(line.find(" yield=2\n") != std::string::npos);
    assert(line.find(" pass_change=0 ") != std::string::npos);
    return 0;
}
