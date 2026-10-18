// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
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
///   new_hot_pages=… vo_waits=… vo_wait_ms=… vo_sleeps=… vo_sleep_ms=…".
struct Counters {
    std::atomic<std::uint64_t> read_faults{};
    std::atomic<std::uint64_t> write_faults{};
    std::atomic<std::uint64_t> fault_ns{};
    std::atomic<std::uint64_t> new_hot_pages{};
    std::atomic<std::uint64_t> vo_waits{};
    std::atomic<std::uint64_t> vo_wait_ns{};
    std::atomic<std::uint64_t> vo_sleeps{};
    std::atomic<std::uint64_t> vo_sleep_ns{};
    std::atomic<std::uint64_t> last_report_ns{};

    void OnFault(bool is_write, std::uint64_t ns) {
        (is_write ? write_faults : read_faults).fetch_add(1, std::memory_order_relaxed);
        fault_ns.fetch_add(ns, std::memory_order_relaxed);
    }

    void OnVoWait(bool slept, std::uint64_t ns) {
        (slept ? vo_sleeps : vo_waits).fetch_add(1, std::memory_order_relaxed);
        (slept ? vo_sleep_ns : vo_wait_ns).fetch_add(ns, std::memory_order_relaxed);
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
               " new_hot_pages=" + std::to_string(new_hot_pages.exchange(0)) +
               " vo_waits=" + std::to_string(vo_waits.exchange(0)) +
               " vo_wait_ms=" + ms(vo_wait_ns.exchange(0)) +
               " vo_sleeps=" + std::to_string(vo_sleeps.exchange(0)) +
               " vo_sleep_ms=" + ms(vo_sleep_ns.exchange(0)) + "\n";
    }
};

inline bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("SHADPS4_FRAME_PATH_STATS");
        return value != nullptr && std::string_view{value} == "1";
    }();
    return enabled;
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
