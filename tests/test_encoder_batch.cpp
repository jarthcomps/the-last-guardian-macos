// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstdint>
#include <deque>
#include <limits>
#include "video_core/renderer_vulkan/vk_encoder_batch.h"

struct Job {
    struct Info {
        std::size_t num_wait_semas{};
        std::size_t num_signal_semas{1};
        std::array<int, 3> signal_semas{7};
        std::array<std::uint64_t, 3> signal_ticks{};
        int fence{};
    } info;
    std::uint64_t signal_value{};
};

int main() {
    using namespace Vulkan;
    if (EncoderBatchLimit(nullptr, 8) != 8 || EncoderBatchLimit(nullptr, 0) != 1 ||
        EncoderBatchLimit("1", 8) != 1 || EncoderBatchLimit("8", 1) != 8) return 1;
    for (const auto* value : {"", "0", "9", "-1", "2x", " 2", "9999999999999999999999999"}) {
        if (EncoderBatchLimit(value, 8) != 1) return 2;
    }
    std::deque<Job> jobs;
    if (EncoderBatchSize(jobs, 7, 8) != 0) return 3;
    for (std::uint64_t tick = 1; tick <= 10; ++tick) {
        Job job;
        job.signal_value = tick;
        job.info.signal_ticks[0] = tick;
        jobs.push_back(job);
        if (EncoderBatchSize(jobs, 7, 8) != std::min(jobs.size(), std::size_t{8})) return 4;
    }
    if (EncoderBatchSize(jobs, 7, 1) != 1 || EncoderBatchSize(jobs, 7, 3) != 3 ||
        EncoderBatchSize(jobs, 7, 100) != 8) return 5;
    const auto original = jobs[2];
    for (int failure = 0; failure < 7; ++failure) {
        jobs[2] = original;
        switch (failure) {
        case 0: jobs[2].info.num_wait_semas = 1; break;
        case 1: jobs[2].info.num_signal_semas = 2; break;
        case 2: jobs[2].info.signal_semas[0] = 9; break;
        case 3: jobs[2].info.signal_ticks[0] = 4; break;
        case 4: jobs[2].info.fence = 1; break;
        case 5: jobs[2].signal_value = 4; jobs[2].info.signal_ticks[0] = 4; break;
        case 6: jobs[2].signal_value = 1; jobs[2].info.signal_ticks[0] = 1; break;
        }
        if (EncoderBatchSize(jobs, 7, 8) != 2) return 6;
        std::swap(jobs[0], jobs[2]);
        if (EncoderBatchSize(jobs, 7, 8) != (failure == 6 ? 2 : 1)) return 7;
        std::swap(jobs[0], jobs[2]);
    }
    jobs[2] = original;
    const auto count = EncoderBatchSize(jobs, 7, 8);
    const auto signal = jobs[count - 1].signal_value;
    // A single maximum signal releases the original milestones in FIFO order, only after all
    // lists in the batch are complete; subsequent queued lists remain pending.
    std::uint64_t callback_tick = 1;
    for (const auto& job : jobs) {
        if (job.signal_value <= signal && job.signal_value != callback_tick++) return 8;
    }
    if (callback_tick != 9 || jobs.front().signal_value != 1 || jobs.back().signal_value != 10) return 9;
    jobs.front().signal_value = std::numeric_limits<std::uint64_t>::max();
    jobs.front().info.signal_ticks[0] = jobs.front().signal_value;
    if (EncoderBatchSize(jobs, 7, 8) != 1) return 10;
    return 0;
}
