// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace Libraries::Kernel {

/// Where guest threads block, by wait object (SHADPS4_WAIT_STATS=1, diagnostics). Each entry
/// counts waits and the time spent in them; a summary line per key is printed about once per
/// second as "WAIT_STATS <key> n=<count> ms=<total>".
class WaitStats {
public:
    void Record(const std::string& key, std::uint64_t ns) {
        std::scoped_lock lk{mutex};
        auto& entry = entries[key];
        ++entry.first;
        entry.second += ns;
    }

    /// Returns the summary of everything recorded since the last call and clears it, when at
    /// least `interval_ns` passed since the previous summary; empty otherwise.
    std::string TakeSummary(std::uint64_t now_ns, std::uint64_t interval_ns = 1'000'000'000ULL) {
        std::scoped_lock lk{mutex};
        if (last_ns == 0) {
            last_ns = now_ns;
            return {};
        }
        if (now_ns - last_ns < interval_ns) {
            return {};
        }
        std::string out;
        for (const auto& [key, entry] : entries) {
            out += "WAIT_STATS " + key + " n=" + std::to_string(entry.first) +
                   " ms=" + std::to_string(entry.second / 1'000'000) + "\n";
        }
        entries.clear();
        last_ns = now_ns;
        return out;
    }

private:
    std::mutex mutex;
    std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> entries;
    std::uint64_t last_ns{};
};

/// The process-wide guest wait statistics.
WaitStats& GuestWaitStats();

} // namespace Libraries::Kernel
