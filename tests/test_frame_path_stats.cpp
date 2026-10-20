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
    Counters::Add(c.cp_idle_ns, 7'000'000);
    Counters::Add(c.handoff_wait_ns, 300'000);
    Counters::Add(c.encoder_ns, 12'340'000);
    Counters::Add(c.gpu_busy_ns, 25'000'000);
    c.OnSubmit(VideoCore::FramePathStats::SubmitReason::Fence);
    c.OnSubmit(VideoCore::FramePathStats::SubmitReason::Fence);
    c.OnSubmit(VideoCore::FramePathStats::SubmitReason::SubmitEnd);
    assert(c.TakeReport(10 + 999'999'999).empty());
    c.image_bytes = 300ULL << 20;
    c.image_count = 1200;
    c.buffer_bytes = 512ULL << 20;
    c.device_bytes = 3ULL << 30;
    const VideoCore::FramePathStats::HostMemory host{8ULL << 30, 100ULL << 20, 2ULL << 30, 2};
    const std::string line = c.TakeReport(10 + 1'000'000'000ULL, [&] { return host; });
    assert(line == "FRAME_PATH_STATS dt=1000.0ms read_faults=1 write_faults=2 fault_ms=2.0 "
                   "read_fault_ms=1.9 slow_write_faults=0 new_hot_pages=3 vo_waits=1 vo_wait_ms=6.0 "
                   "vo_sleeps=1 vo_sleep_ms=2.5 draws=4 gs_draws=2 tess_draws=2 cp_idle_ms=7.0 "
                   "handoff_wait_ms=0.3 encoder_ms=12.3 gpu_busy_ms=25.0 submits_other=0 submits_fence=2 "
                   "submits_end=1 submits_readback=0 submits_wait=0 submits_finish=0 "
                   "submits_present=0 footprint_mb=8192 free_mb=100 compressor_mb=2048 device_mb=3072 "
                   "images_mb=300 images=1200 buffers_mb=512 thermal=2\n");
    c.image_bytes = 0;
    c.image_count = 0;
    c.buffer_bytes = 0;
    c.device_bytes = 0;
    assert(c.TakeReport(10 + 2'000'000'000ULL, [] { return VideoCore::FramePathStats::HostMemory{}; }) ==
           "FRAME_PATH_STATS dt=1000.0ms read_faults=0 write_faults=0 fault_ms=0.0 "
           "read_fault_ms=0.0 slow_write_faults=0 new_hot_pages=0 vo_waits=0 vo_wait_ms=0.0 vo_sleeps=0 vo_sleep_ms=0.0 draws=0 gs_draws=0 tess_draws=0 "
           "cp_idle_ms=0.0 handoff_wait_ms=0.0 encoder_ms=0.0 gpu_busy_ms=0.0 submits_other=0 submits_fence=0 submits_end=0 submits_readback=0 submits_wait=0 submits_finish=0 submits_present=0 footprint_mb=0 free_mb=0 compressor_mb=0 device_mb=0 images_mb=0 images=0 buffers_mb=0 thermal=0\n");
    // The real reader returns this process's footprint on macOS.
#ifdef __APPLE__
    assert(VideoCore::FramePathStats::ReadHostMemory().footprint_bytes > 0);
    assert(VideoCore::FramePathStats::ReadHostMemory().thermal_level <= 4);
#endif
    assert(!VideoCore::FramePathStats::Enabled()); // SHADPS4_FRAME_PATH_STATS unset
    assert(VideoCore::FramePathStats::Level() == 0);
    {
        using VideoCore::FramePathStats::g_submit_reason;
        using VideoCore::FramePathStats::SubmitReason;
        using VideoCore::FramePathStats::SubmitReasonScope;
        assert(g_submit_reason == SubmitReason::Other);
        {
            SubmitReasonScope fence{SubmitReason::Fence};
            assert(g_submit_reason == SubmitReason::Fence);
            {
                SubmitReasonScope wait{SubmitReason::Wait};
                assert(g_submit_reason == SubmitReason::Wait);
            }
            assert(g_submit_reason == SubmitReason::Fence);
        }
        assert(g_submit_reason == SubmitReason::Other);
    }
    VideoCore::FramePathStats::FaultBlocks blocks;
    for (int i = 0; i < 5; ++i) blocks.Add(0x32ee88010 + i * 8); // one 64 KB block
    blocks.Add(0x27c600000);
    blocks.Add(0x27c60ffff);
    assert(blocks.Take(1) == "FAULT_BLOCKS blocks=2 0x32ee80000:5\n");
    assert(blocks.Take() == "FAULT_BLOCKS blocks=0\n");
    return 0;
}
