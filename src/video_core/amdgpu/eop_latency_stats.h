// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace AmdGpu {

/// Where the time between a guest graphics submission and its end-of-pipe (EOP) signal goes
/// (SHADPS4_EOP_STATS=1, diagnostics). The Last Guardian's render thread waits for the EOP of
/// each frame before building the next one, so this latency is the frame's critical path.
///   submit -> cp:   the command processor reaches the EOP packet (PM4 translation of the frame)
///   cp -> gpu:      the GPU finishes the work recorded before it (deferred operation starts)
///   gpu -> signal:  readback write-backs land and the fence is written
///   signal -> submit: the game builds the next frame after waking up (guest CPU time)
/// Only EOPs that raise an interrupt (the ones the game waits for) are counted. Per frame it
/// also counts guest submissions, Vulkan submissions (scheduler ticks) and the GPU backlog when
/// the command processor reaches the EOP: submissions the GPU has not finished yet. A backlog
/// near zero means the GPU waited on the command processor, and the cp -> gpu tail is the work
/// recorded since the last submission.
struct EopLatencyStats {
    std::atomic<std::uint64_t> count{};
    std::atomic<std::uint64_t> submit_to_cp_ns{};
    std::atomic<std::uint64_t> cp_to_gpu_ns{};
    std::atomic<std::uint64_t> gpu_to_signal_ns{};
    std::atomic<std::uint64_t> resume_count{};
    std::atomic<std::uint64_t> signal_to_submit_ns{};
    std::atomic<std::uint64_t> last_signal_ns{};
    std::atomic<std::uint64_t> last_report_ns{};
    std::atomic<std::uint64_t> guest_submits{};
    std::atomic<std::uint64_t> tick_frames{};
    std::atomic<std::uint64_t> vk_submits{};
    std::atomic<std::uint64_t> backlog{};
    std::atomic<std::uint64_t> encoder_backlog{};
    std::atomic<std::uint64_t> last_eop_tick{};

    /// A guest graphics submission at `now_ns`: the first one after an EOP signal ends the
    /// game's CPU time for the next frame.
    void OnSubmit(std::uint64_t now_ns) {
        guest_submits.fetch_add(1, std::memory_order_relaxed);
        const std::uint64_t signal = last_signal_ns.exchange(0, std::memory_order_relaxed);
        if (signal != 0 && now_ns > signal) {
            resume_count.fetch_add(1, std::memory_order_relaxed);
            signal_to_submit_ns.fetch_add(now_ns - signal, std::memory_order_relaxed);
        }
    }

    /// The command processor reached an interrupting EOP while the scheduler was recording
    /// `current_tick`, the GPU had finished `gpu_tick` and `encoder_jobs` recorded command
    /// lists were still waiting for the encoder thread (not yet submitted to the GPU).
    void OnCpTicks(std::uint64_t current_tick, std::uint64_t gpu_tick,
                   std::uint64_t encoder_jobs = 0) {
        const std::uint64_t last = last_eop_tick.exchange(current_tick, std::memory_order_relaxed);
        if (last != 0 && current_tick > last) {
            tick_frames.fetch_add(1, std::memory_order_relaxed);
            vk_submits.fetch_add(current_tick - last, std::memory_order_relaxed);
            backlog.fetch_add(current_tick > gpu_tick + 1 ? current_tick - 1 - gpu_tick : 0,
                              std::memory_order_relaxed);
            encoder_backlog.fetch_add(encoder_jobs, std::memory_order_relaxed);
        }
    }

    void Add(std::uint64_t submit_ns, std::uint64_t cp_ns, std::uint64_t gpu_ns,
             std::uint64_t signal_ns) {
        count.fetch_add(1, std::memory_order_relaxed);
        submit_to_cp_ns.fetch_add(cp_ns > submit_ns ? cp_ns - submit_ns : 0,
                                  std::memory_order_relaxed);
        cp_to_gpu_ns.fetch_add(gpu_ns > cp_ns ? gpu_ns - cp_ns : 0, std::memory_order_relaxed);
        gpu_to_signal_ns.fetch_add(signal_ns > gpu_ns ? signal_ns - gpu_ns : 0,
                                   std::memory_order_relaxed);
        last_signal_ns.store(signal_ns, std::memory_order_relaxed);
    }

    /// Averages since the previous report as one line, when a second passed; empty otherwise.
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
        const std::uint64_t n = count.exchange(0);
        const std::uint64_t a = submit_to_cp_ns.exchange(0);
        const std::uint64_t b = cp_to_gpu_ns.exchange(0);
        const std::uint64_t c = gpu_to_signal_ns.exchange(0);
        const std::uint64_t r = resume_count.exchange(0);
        const std::uint64_t d = signal_to_submit_ns.exchange(0);
        const std::uint64_t g = guest_submits.exchange(0);
        const std::uint64_t f = tick_frames.exchange(0);
        const std::uint64_t v = vk_submits.exchange(0);
        const std::uint64_t q = backlog.exchange(0);
        const std::uint64_t e = encoder_backlog.exchange(0);
        if (n == 0) {
            return "EOP_STATS n=0\n";
        }
        const auto ms = [](std::uint64_t ns, std::uint64_t k) {
            if (k == 0) {
                return std::string{"-"};
            }
            return std::to_string(ns / k / 1'000'000) + "." +
                   std::to_string(ns / k / 100'000 % 10);
        };
        const auto avg = [](std::uint64_t sum, std::uint64_t k) {
            if (k == 0) {
                return std::string{"-"};
            }
            return std::to_string(sum / k) + "." + std::to_string(sum * 10 / k % 10);
        };
        return "EOP_STATS n=" + std::to_string(n) + " submit_to_cp_ms=" + ms(a, n) +
               " cp_to_gpu_ms=" + ms(b, n) + " gpu_to_signal_ms=" + ms(c, n) +
               " signal_to_submit_ms=" + ms(d, r) + " guest_submits=" + avg(g, n) +
               " vk_submits=" + avg(v, f) + " gpu_backlog=" + avg(q, f) +
               " encoder_backlog=" + avg(e, f) + "\n";
    }
};

/// The process-wide instance.
inline EopLatencyStats& GlobalEopLatencyStats() {
    static EopLatencyStats stats;
    return stats;
}

} // namespace AmdGpu
