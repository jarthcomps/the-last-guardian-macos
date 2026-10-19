// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

namespace VideoCore {

/// Byte ranges of one buffer that GPU commands read or wrote since the last full GPU barrier
/// (SHADPS4_PRECISE_BUFFER_HAZARDS=1, Metal only).
///
/// The buffer cache tracks hazards per whole buffer: a dispatch writing a few bytes of a large
/// buffer makes the next dispatch touching any other part of it wait. On KosmicKrisp every
/// barrier recorded inside a compute encoder, and every encoder end, waits for all earlier work,
/// so an access is also ordered once any later barrier was recorded. `epoch` counts those full
/// barriers; ranges recorded in an older epoch are already ordered and are dropped.
///
/// Cost on the command thread: every access to a buffer with history is committed, so `Add`
/// only tries to merge with the most recent ranges (draws re-read the same ranges back to back)
/// instead of scanning all of them. A buffer has history only once a tracked (allowlisted)
/// dispatch touched it; until then nothing is recorded for it and its first tracked access gets
/// the normal per-buffer barrier, so the history it starts is complete.
class BufferHazardTracker {
public:
    static constexpr std::size_t MaxRanges = 256;
    static constexpr std::size_t MergeWindow = 8; ///< most recent ranges `Add` merges into

    bool HasHistory() const {
        return history;
    }

    /// From now on every access is committed (see the class comment).
    void StartHistory() {
        history = true;
    }
    using Range = std::pair<std::uint64_t, std::uint64_t>; ///< [begin, end)

    /// Whether an access of [begin, end) needs a barrier before it.
    bool Hazard(std::uint64_t now_epoch, std::uint64_t begin, std::uint64_t end,
                bool is_write) const {
        if (epoch != now_epoch) {
            return false;
        }
        return Overlaps(writes, num_writes, begin, end) ||
               (is_write && Overlaps(reads, num_reads, begin, end));
    }

    /// Records an access of [begin, end) after the command doing it was recorded.
    void Commit(std::uint64_t now_epoch, std::uint64_t begin, std::uint64_t end, bool is_write) {
        if (epoch != now_epoch) {
            epoch = now_epoch;
            num_writes = 0;
            num_reads = 0;
        }
        if (is_write) {
            Add(writes, num_writes, begin, end);
        } else {
            Add(reads, num_reads, begin, end);
        }
    }

private:
    static bool Overlaps(const std::array<Range, MaxRanges>& ranges, std::size_t count,
                         std::uint64_t begin, std::uint64_t end) {
        for (std::size_t i = 0; i < count; ++i) {
            if (ranges[i].first < end && begin < ranges[i].second) {
                return true;
            }
        }
        return false;
    }

    static void Add(std::array<Range, MaxRanges>& ranges, std::size_t& count, std::uint64_t begin,
                    std::uint64_t end) {
        for (std::size_t i = count; i-- > (count > MergeWindow ? count - MergeWindow : 0);) {
            // Merge with an overlapping or touching range.
            if (ranges[i].first <= end && begin <= ranges[i].second) {
                ranges[i].first = std::min(ranges[i].first, begin);
                ranges[i].second = std::max(ranges[i].second, end);
                return;
            }
        }
        if (count < MaxRanges) {
            ranges[count++] = {begin, end};
            return;
        }
        // Full: widen the last range to cover the new one (conservative).
        ranges[count - 1].first = std::min(ranges[count - 1].first, begin);
        ranges[count - 1].second = std::max(ranges[count - 1].second, end);
    }

    std::uint64_t epoch = ~std::uint64_t{0};
    std::array<Range, MaxRanges> writes{};
    std::array<Range, MaxRanges> reads{};
    std::size_t num_writes = 0;
    std::size_t num_reads = 0;
    bool history = false;
};

} // namespace VideoCore
