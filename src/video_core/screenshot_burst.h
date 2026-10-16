// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

namespace VideoCore {

/// One frame of a screenshot burst.
struct BurstFrame {
    std::uint32_t burst_id{};
    std::uint32_t index{};
};

/// Hands out consecutive frame indices for one screenshot burst at a time. Start() is called from
/// the IPC thread, Next() once per presented frame from the presenter.
class ScreenshotBurstCounter {
public:
    /// Starts a burst of `frames` frames. Returns its id (1, 2, ...), or 0 when a burst is still
    /// running or `frames` is 0.
    std::uint32_t Start(std::uint32_t frames) {
        if (frames == 0 || remaining.load(std::memory_order_acquire) != 0) {
            return 0;
        }
        const std::uint32_t id = next_id.fetch_add(1, std::memory_order_relaxed) + 1;
        current_id.store(id, std::memory_order_relaxed);
        total.store(frames, std::memory_order_relaxed);
        remaining.store(frames, std::memory_order_release);
        return id;
    }

    /// The frame to capture now, if a burst is running.
    std::optional<BurstFrame> Next() {
        std::uint32_t left = remaining.load(std::memory_order_acquire);
        while (left != 0) {
            if (remaining.compare_exchange_weak(left, left - 1, std::memory_order_acq_rel)) {
                return BurstFrame{current_id.load(std::memory_order_relaxed),
                                  total.load(std::memory_order_relaxed) - left};
            }
        }
        return std::nullopt;
    }

private:
    std::atomic<std::uint32_t> next_id{0};
    std::atomic<std::uint32_t> current_id{0};
    std::atomic<std::uint32_t> total{0};
    std::atomic<std::uint32_t> remaining{0};
};

} // namespace VideoCore
