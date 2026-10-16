// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cassert>
#include <string>

#include "video_core/amdgpu/eop_latency_stats.h"

int main() {
    AmdGpu::EopLatencyStats stats;
    assert(stats.TakeReport(1000).empty());
    stats.Add(0, 12'000'000, 30'000'000, 31'500'000);   // 12 / 18 / 1.5 ms
    stats.Add(0, 14'000'000, 36'000'000, 38'500'000);   // 14 / 22 / 2.5 ms
    stats.OnSubmit(45'000'000);                         // game resumes 6.5 ms after the signal
    stats.OnSubmit(46'000'000);                         // later submits of the same frame: ignored
    stats.Add(100, 50, 40, 30);                         // clock skew counts as 0
    stats.OnCpTicks(10, 9);                             // first EOP: no previous tick yet
    stats.OnCpTicks(16, 12);                            // 6 submits, GPU 3 behind (13..15)
    stats.OnCpTicks(20, 19);                            // 4 submits, GPU caught up
    assert(stats.TakeReport(1000 + 999'999'999).empty());
    const std::string line = stats.TakeReport(1000 + 1'000'000'000ULL);
    assert(line == "EOP_STATS n=3 submit_to_cp_ms=8.6 cp_to_gpu_ms=13.3 gpu_to_signal_ms=1.3 "
                   "signal_to_submit_ms=6.5 guest_submits=0.6 vk_submits=5.0 gpu_backlog=1.5\n");
    assert(stats.TakeReport(1000 + 2'000'000'000ULL) == "EOP_STATS n=0\n");
    return 0;
}
