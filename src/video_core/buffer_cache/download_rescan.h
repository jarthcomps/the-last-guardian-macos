// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <cstdint>

namespace VideoCore {
// Load the epoch BEFORE the locked predicate. A completion between the two causes at
// worst an extra scan, never a missed completion. Mutators publish while holding that lock.
class DownloadRescan {
public:
    bool Pending(const std::atomic<std::uint64_t>& changes, bool enabled, auto&& predicate) {
        const auto current = changes.load(std::memory_order_acquire);
        if (!enabled || !initialized || current != observed) {
            observed = current;
            initialized = true;
            pending = predicate();
        }
        return pending;
    }
private:
    std::uint64_t observed{};
    bool initialized{}, pending{};
};
} // namespace VideoCore
