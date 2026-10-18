// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cassert>
#include <string>

#include "video_core/frame_path_stats.h"

using VideoCore::FramePathStats::Counters;

int main() {
    Counters c;
    assert(c.TakeReport(10).empty()); // starts the interval
    c.OnFault(true, 40'000);
    c.OnFault(true, 60'000);
    // (a write fault over 1 ms counts as slow; none here)
    c.OnFault(false, 1'900'000);
    c.new_hot_pages.fetch_add(3);
    c.OnVoWait(false, 6'000'000);
    c.OnVoWait(true, 2'500'000);
    c.OnDraw(0);
    c.OnDraw(0xB0);
    c.OnDraw(0x45);
    c.OnDraw(0xAD);
    assert(c.TakeReport(10 + 999'999'999).empty());
    const std::string line = c.TakeReport(10 + 1'000'000'000ULL);
    assert(line == "FRAME_PATH_STATS dt=1000.0ms read_faults=1 write_faults=2 fault_ms=2.0 "
                   "read_fault_ms=1.9 slow_write_faults=0 new_hot_pages=3 vo_waits=1 vo_wait_ms=6.0 "
                   "vo_sleeps=1 vo_sleep_ms=2.5 draws=4 gs_draws=2 tess_draws=2\n");
    assert(c.TakeReport(10 + 2'000'000'000ULL) ==
           "FRAME_PATH_STATS dt=1000.0ms read_faults=0 write_faults=0 fault_ms=0.0 "
           "read_fault_ms=0.0 slow_write_faults=0 new_hot_pages=0 vo_waits=0 vo_wait_ms=0.0 vo_sleeps=0 vo_sleep_ms=0.0 draws=0 gs_draws=0 tess_draws=0\n");
    assert(!VideoCore::FramePathStats::Enabled()); // SHADPS4_FRAME_PATH_STATS unset
    assert(VideoCore::FramePathStats::Level() == 0);
    VideoCore::FramePathStats::FaultBlocks blocks;
    for (int i = 0; i < 5; ++i) blocks.Add(0x32ee88010 + i * 8); // one 64 KB block
    blocks.Add(0x27c600000);
    blocks.Add(0x27c60ffff);
    assert(blocks.Take(1) == "FAULT_BLOCKS blocks=2 0x32ee80000:5\n");
    assert(blocks.Take() == "FAULT_BLOCKS blocks=0\n");
    return 0;
}
