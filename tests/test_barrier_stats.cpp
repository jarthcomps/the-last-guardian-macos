// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cassert>
#include <string>

#include "video_core/renderer_vulkan/barrier_stats.h"

using namespace Vulkan::BarrierStats;

int main() {
    static_assert(Names.size() == static_cast<std::size_t>(Source::Count));
    for (const auto name : Names) {
        assert(!name.empty());
    }
    Counters counters;
    assert(counters.TakeReport(5).empty()); // first call starts the interval
    assert(current == Source::Other);
    {
        Scope upload{Source::Upload};
        counters.Add(current);
        counters.Add(current);
        {
            Scope nested{Source::Resources};
            counters.Add(current);
        }
        assert(current == Source::Upload);
    }
    assert(current == Source::Other);
    counters.Add(current);
    assert(counters.TakeReport(5 + 999'999'999).empty());
    assert(counters.TakeReport(5 + 1'000'000'000ULL) ==
           "BARRIER_STATS other=1 resources=1 upload=2 total=4\n");
    assert(counters.TakeReport(5 + 2'000'000'000ULL) == "BARRIER_STATS total=0\n");
    assert(ParseSourceMask("") == 0);
    assert(ParseSourceMask("upload") == 1u << static_cast<int>(Source::Upload));
    assert(ParseSourceMask("upload,image_transit,bogus") ==
           ((1u << static_cast<int>(Source::Upload)) |
            (1u << static_cast<int>(Source::ImageTransit))));
    assert(DropMask() == 0); // SHADPS4_DROP_BARRIERS unset
    // Full barriers only count on the command processor thread, outside render passes.
    const std::uint64_t e0 = SyncEpoch();
    assert(OnBarrier());
    assert(SyncEpoch() == e0); // not the command processor thread
    sync_thread = true;
    bool in_pass = true;
    rendering = &in_pass;
    assert(OnBarrier());
    assert(SyncEpoch() == e0); // inside a render pass: not a full barrier
    in_pass = false;
    assert(OnBarrier());
    assert(SyncEpoch() == e0 + 1);
    OnFullSync(); // end of a render pass
    assert(SyncEpoch() == e0 + 2);
    return 0;
}
