// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <charconv>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <limits>
#include <string_view>

namespace Vulkan {

inline constexpr std::size_t MaxEncoderBatchSize = 8;

inline std::size_t EncoderBatchLimit(const char* setting, std::size_t default_limit) {
    if (!setting) {
        return std::clamp(default_limit, std::size_t{1}, MaxEncoderBatchSize);
    }
    const std::string_view value{setting};
    std::size_t limit{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), limit);
    if (error != std::errc{} || end != value.data() + value.size() || limit == 0 ||
        limit > MaxEncoderBatchSize) {
        return 1;
    }
    return limit;
}

/// Encoder threads replaying command lists in parallel (SHADPS4_ENCODER_THREADS, 1 to
/// MaxEncoderThreads, default 1). Each replays a batch into its own command buffer; the
/// batches are still submitted in the order the command processor recorded them.
inline constexpr std::size_t MaxEncoderThreads = 4;

inline std::size_t EncoderThreadCount(const char* setting) {
    if (!setting) {
        return 1;
    }
    const std::string_view value{setting};
    std::size_t count{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), count);
    if (error != std::errc{} || end != value.data() + value.size() || count == 0) {
        return 1;
    }
    return std::min(count, MaxEncoderThreads);
}

/// Lets parallel encoder threads submit in ticket order: Take() hands out tickets while the
/// job queue is locked, Enter(ticket) waits until every earlier ticket has left.
class SubmitTurnstile {
public:
    std::uint64_t Take() {
        return next_ticket++;
    }

    template <typename Body>
    void Enter(std::uint64_t ticket, Body&& body) {
        {
            std::unique_lock lk{mutex};
            cv.wait(lk, [&] { return serving == ticket; });
        }
        body();
        {
            std::scoped_lock lk{mutex};
            ++serving;
        }
        cv.notify_all();
    }

private:
    std::uint64_t next_ticket{}; ///< guarded by the caller's job-queue lock
    std::uint64_t serving{};
    std::mutex mutex;
    std::condition_variable cv;
};

// Only coalesce a consecutive prefix that signals this scheduler's timeline alone. External
// waits, presentation signals, and fences keep their original submission boundaries.
template <typename Jobs, typename Semaphore>
std::size_t EncoderBatchSize(const Jobs& jobs, Semaphore master, std::size_t limit) {
    if (jobs.empty()) {
        return 0;
    }
    const auto eligible = [master](const auto& job) {
        return job.signal_value > 0 && job.info.num_wait_semas == 0 &&
               job.info.num_signal_semas == 1 && job.info.signal_semas[0] == master &&
               job.info.signal_ticks[0] == job.signal_value && !job.info.fence;
    };
    std::size_t count = 1;
    const auto maximum =
        std::min(jobs.size(), std::clamp(limit, std::size_t{1}, MaxEncoderBatchSize));
    if (!eligible(jobs.front())) {
        return count;
    }
    while (count < maximum && eligible(jobs[count]) &&
           jobs[count - 1].signal_value !=
               std::numeric_limits<decltype(jobs.front().signal_value)>::max() &&
           jobs[count].signal_value == jobs[count - 1].signal_value + 1) {
        ++count;
    }
    return count;
}

} // namespace Vulkan
