// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace VideoCore::FramePathStats {

/// Cheap per-second counters for the costs on The Last Guardian's outdoor critical path
/// (SHADPS4_FRAME_PATH_STATS=1, diagnostics; unlike the readback profiler they add almost no
/// overhead, so they can run during FPS measurements):
///   guest page faults: write faults are CPU writes to write-protected (uploaded) pages, read
///     faults are CPU reads of pages the GPU wrote; both stall the faulting guest thread.
///   hot pages: pages that became hot write pages (readback_hot_write_pages_enabled).
///   video-out label waits: the graphics ring waiting in WaitRegMem for the presenter to release
///     a flip buffer, by yielding (other rings had work) or sleeping (nothing else to do).
/// One line per second: "FRAME_PATH_STATS dt=… read_faults=… write_faults=… fault_ms=…
///   new_hot_pages=… vo_waits=… vo_wait_ms=… vo_sleeps=… vo_sleep_ms=… draws=… cp_idle_ms=…
///   handoff_wait_ms=… encoder_ms=… gpu_busy_ms=…". The last four split the frame's serial path:
///   command processor idle (no guest work) or waiting on the encoder queue, encoder thread busy,
///   and GPU busy (level 3 only, timestamp queries per submission).
struct Counters {
    std::atomic<std::uint64_t> read_faults{};
    std::atomic<std::uint64_t> write_faults{};
    std::atomic<std::uint64_t> fault_ns{};
    std::atomic<std::uint64_t> read_fault_ns{};
    std::atomic<std::uint64_t> slow_write_faults{}; ///< over 1 ms: waited for GPU data
    std::atomic<std::uint64_t> new_hot_pages{};
    std::atomic<std::uint64_t> vo_waits{};
    std::atomic<std::uint64_t> vo_wait_ns{};
    std::atomic<std::uint64_t> vo_sleeps{};
    std::atomic<std::uint64_t> vo_sleep_ns{};
    std::atomic<std::uint64_t> draws{};      ///< draws reaching the GPU
    std::atomic<std::uint64_t> gs_draws{};   ///< with a geometry shader (compute pre-pass on Metal)
    std::atomic<std::uint64_t> tess_draws{}; ///< with tessellation (compute pre-pass on Metal)
    std::atomic<std::uint64_t> cp_idle_ns{};      ///< command processor out of guest work
    std::atomic<std::uint64_t> handoff_wait_ns{}; ///< command processor waiting for the encoder
    std::atomic<std::uint64_t> encoder_ns{};      ///< encoder replaying and submitting
    std::atomic<std::uint64_t> gpu_busy_ns{};     ///< GPU time (level 3: timestamp queries)
    std::atomic<std::uint64_t> last_report_ns{};

    void OnFault(bool is_write, std::uint64_t ns) {
        (is_write ? write_faults : read_faults).fetch_add(1, std::memory_order_relaxed);
        fault_ns.fetch_add(ns, std::memory_order_relaxed);
        if (!is_write) {
            read_fault_ns.fetch_add(ns, std::memory_order_relaxed);
        } else if (ns > 1'000'000) {
            slow_write_faults.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void OnVoWait(bool slept, std::uint64_t ns) {
        (slept ? vo_sleeps : vo_waits).fetch_add(1, std::memory_order_relaxed);
        (slept ? vo_sleep_ns : vo_wait_ns).fetch_add(ns, std::memory_order_relaxed);
    }

    /// A draw with the given VGT stage-enable value (0 = VS only, 0xB0 = ES/GS, 0x45 = LS/HS,
    /// 0xAD = LS/HS/ES/GS).
    void OnDraw(std::uint32_t stage_enable) {
        draws.fetch_add(1, std::memory_order_relaxed);
        if (stage_enable == 0xB0u || stage_enable == 0xADu) {
            gs_draws.fetch_add(1, std::memory_order_relaxed);
        }
        if (stage_enable == 0x45u || stage_enable == 0xADu) {
            tess_draws.fetch_add(1, std::memory_order_relaxed);
        }
    }

    static void Add(std::atomic<std::uint64_t>& counter, std::uint64_t ns) {
        counter.fetch_add(ns, std::memory_order_relaxed);
    }

    /// The counts since the previous report as one line once a second passed, empty otherwise.
    std::string TakeReport(std::uint64_t now_ns) {
        std::uint64_t last = last_report_ns.load(std::memory_order_relaxed);
        if (last == 0) {
            last_report_ns.store(now_ns, std::memory_order_relaxed);
            return {};
        }
        if (now_ns - last < 1'000'000'000ULL ||
            !last_report_ns.compare_exchange_strong(last, now_ns)) {
            return {};
        }
        const auto ms = [](std::uint64_t ns) {
            return std::to_string(ns / 1'000'000) + "." + std::to_string(ns / 100'000 % 10);
        };
        const std::uint64_t dt = now_ns - last;
        return "FRAME_PATH_STATS dt=" + ms(dt) + "ms read_faults=" +
               std::to_string(read_faults.exchange(0)) +
               " write_faults=" + std::to_string(write_faults.exchange(0)) +
               " fault_ms=" + ms(fault_ns.exchange(0)) +
               " read_fault_ms=" + ms(read_fault_ns.exchange(0)) +
               " slow_write_faults=" + std::to_string(slow_write_faults.exchange(0)) +
               " new_hot_pages=" + std::to_string(new_hot_pages.exchange(0)) +
               " vo_waits=" + std::to_string(vo_waits.exchange(0)) +
               " vo_wait_ms=" + ms(vo_wait_ns.exchange(0)) +
               " vo_sleeps=" + std::to_string(vo_sleeps.exchange(0)) +
               " vo_sleep_ms=" + ms(vo_sleep_ns.exchange(0)) +
               " draws=" + std::to_string(draws.exchange(0)) +
               " gs_draws=" + std::to_string(gs_draws.exchange(0)) +
               " tess_draws=" + std::to_string(tess_draws.exchange(0)) +
               " cp_idle_ms=" + ms(cp_idle_ns.exchange(0)) +
               " handoff_wait_ms=" + ms(handoff_wait_ns.exchange(0)) +
               " encoder_ms=" + ms(encoder_ns.exchange(0)) +
               " gpu_busy_ms=" + ms(gpu_busy_ns.exchange(0)) + "\n";
    }
};

/// SHADPS4_FRAME_PATH_STATS=2 also lists, once per second, the 64 KB blocks that took the most
/// write faults: "FAULT_BLOCKS 0x…:count …" (shows whether faults hit the same pages every
/// frame, which hot write pages can learn, or new memory each frame).
class FaultBlocks {
public:
    void Add(std::uint64_t address) {
        std::scoped_lock lk{mutex};
        ++counts[address >> 16];
    }

    /// The `limit` busiest blocks since the previous call, busiest first, and clears them.
    std::string Take(std::size_t limit = 8) {
        std::vector<std::pair<std::uint64_t, std::uint64_t>> top;
        {
            std::scoped_lock lk{mutex};
            top.assign(counts.begin(), counts.end());
            counts.clear();
        }
        std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
        std::string out = "FAULT_BLOCKS blocks=" + std::to_string(top.size());
        char buffer[48];
        for (std::size_t i = 0; i < std::min(limit, top.size()); ++i) {
            std::snprintf(buffer, sizeof(buffer), " 0x%llx:%llu",
                          static_cast<unsigned long long>(top[i].first << 16),
                          static_cast<unsigned long long>(top[i].second));
            out += buffer;
        }
        return out + "\n";
    }

private:
    std::mutex mutex;
    std::unordered_map<std::uint64_t, std::uint64_t> counts;
};

inline int Level() {
    static const int level = [] {
        const char* value = std::getenv("SHADPS4_FRAME_PATH_STATS");
        return value != nullptr ? std::atoi(value) : 0;
    }();
    return level;
}

inline FaultBlocks& GlobalFaultBlocks() {
    static FaultBlocks blocks;
    return blocks;
}

inline bool Enabled() {
    return Level() >= 1;
}

inline Counters& Global() {
    static Counters counters;
    return counters;
}

inline std::uint64_t NowNs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

} // namespace VideoCore::FramePathStats
