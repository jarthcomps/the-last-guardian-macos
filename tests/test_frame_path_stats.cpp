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
    c.OnFault(false, 1'900'000);
    c.new_hot_pages.fetch_add(3);
    c.OnVoWait(false, 6'000'000);
    c.OnVoWait(true, 2'500'000);
    assert(c.TakeReport(10 + 999'999'999).empty());
    const std::string line = c.TakeReport(10 + 1'000'000'000ULL);
    assert(line == "FRAME_PATH_STATS dt=1000.0ms read_faults=1 write_faults=2 fault_ms=2.0 "
                   "new_hot_pages=3 vo_waits=1 vo_wait_ms=6.0 vo_sleeps=1 vo_sleep_ms=2.5\n");
    assert(c.TakeReport(10 + 2'000'000'000ULL) ==
           "FRAME_PATH_STATS dt=1000.0ms read_faults=0 write_faults=0 fault_ms=0.0 "
           "new_hot_pages=0 vo_waits=0 vo_wait_ms=0.0 vo_sleeps=0 vo_sleep_ms=0.0\n");
    assert(!VideoCore::FramePathStats::Enabled()); // SHADPS4_FRAME_PATH_STATS unset
    return 0;
}
