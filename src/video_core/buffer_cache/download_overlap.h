// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace VideoCore {

// Downloads protect whole pages, but only these half-open byte ranges are copied back.
// A shader header elsewhere on the page can still be read from its current CPU backing.
inline bool ReadbackCopiesOverlap(
    std::span<const std::pair<std::uintptr_t, std::uintptr_t>> copies,
    std::uintptr_t address, std::uint64_t size) {
    if (size == 0) {
        return false;
    }
    for (const auto& [start, end] : copies) {
        if (start >= end || address >= end) {
            continue;
        }
        // Subtract rather than forming address + size, which could overflow.
        if (start <= address || size > start - address) {
            return true;
        }
    }
    return false;
}

// Add, Remove and Overlaps are serialized by the caller's readback mutex. The registry includes
// ordinary asynchronous copies as well as prefetches, regardless of page-unmark policy.
//
// MayOverlap is a lock-free test in front of Overlaps. It reads a counting filter over 256-byte
// granules, and Add and Remove update that filter under the same mutex. Every counter change is an
// atomic read-modify-write with release order. A reader that loads zero with acquire order
// synchronizes with the decrement that produced that zero, and through the release sequence with
// the earlier decrements on the same counter. So the write backs that a Remove performed before
// those decrements are visible to the reader. A zero means no pending record covers the granule.
// A nonzero value only means "maybe": the caller then takes the mutex and calls Overlaps.
class PendingBackingCopies {
public:
    using Range = std::pair<std::uintptr_t, std::uintptr_t>;

    PendingBackingCopies() : filter{new std::atomic<std::uint32_t>[FilterSlots]()} {}

    void Add(std::uint64_t id, std::span<const Range> copies) {
        records.push_back({id, {copies.begin(), copies.end()}});
        Count(records.back().copies, +1);
    }

    void Remove(std::uint64_t id) {
        for (auto it = records.begin(); it != records.end();) {
            if (it->id == id) {
                Count(it->copies, -1);
                it = records.erase(it);
            } else {
                ++it;
            }
        }
    }

    /// Lock-free. False means no record that is added and not yet removed overlaps the range.
    [[nodiscard]] bool MayOverlap(std::uintptr_t address, std::uint64_t size) const {
        if (size == 0) {
            return false;
        }
        if (wide_records.load(std::memory_order_acquire) != 0) {
            return true;
        }
        if (size - 1 > ~std::uintptr_t{0} - address) {
            return true; // Wraps past the end of the address space; the exact check decides.
        }
        const std::uintptr_t first = address >> GranuleShift;
        const std::uintptr_t last = (address + (size - 1)) >> GranuleShift;
        if (last - first >= MaxProbeGranules) {
            return true;
        }
        for (std::uintptr_t granule = first;; ++granule) {
            if (filter[granule & (FilterSlots - 1)].load(std::memory_order_acquire) != 0) {
                return true;
            }
            if (granule == last) {
                return false;
            }
        }
    }

    /// Exact. The caller holds its readback mutex.
    bool Overlaps(std::uintptr_t address, std::uint64_t size) const {
        for (const auto& record : records) {
            if (ReadbackCopiesOverlap(record.copies, address, size)) {
                return true;
            }
        }
        return false;
    }

    // Granules are 1 << GranuleShift bytes. Granules that differ by FilterSlots share a counter.
    static constexpr std::uintptr_t GranuleShift = 8;
    static constexpr std::size_t FilterSlots = std::size_t{1} << 14;

private:
    static constexpr std::uintptr_t MaxProbeGranules = 64;    // wider queries take the mutex
    static constexpr std::uintptr_t MaxRecordGranules = 4096; // wider records use wide_records

    struct Record {
        std::uint64_t id;
        std::vector<Range> copies;
    };

    // Add and Remove see the same copies, so both make the same choice here.
    static bool IsWide(std::span<const Range> copies) {
        std::uintptr_t granules = 0;
        for (const auto& [start, end] : copies) {
            if (start >= end) {
                continue;
            }
            granules += ((end - 1) >> GranuleShift) - (start >> GranuleShift) + 1;
            if (granules > MaxRecordGranules) {
                return true;
            }
        }
        return false;
    }

    // Adds one count (delta > 0) or removes one (delta < 0) for each granule the copies touch.
    // Called with the caller's mutex held.
    void Count(std::span<const Range> copies, int delta) {
        if (IsWide(copies)) {
            if (delta > 0) {
                wide_records.fetch_add(1, std::memory_order_release);
            } else {
                wide_records.fetch_sub(1, std::memory_order_release);
            }
            return;
        }
        for (const auto& [start, end] : copies) {
            if (start >= end) {
                continue;
            }
            const std::uintptr_t last = (end - 1) >> GranuleShift;
            for (std::uintptr_t granule = start >> GranuleShift;; ++granule) {
                auto& slot = filter[granule & (FilterSlots - 1)];
                if (delta > 0) {
                    slot.fetch_add(1, std::memory_order_release);
                } else {
                    slot.fetch_sub(1, std::memory_order_release);
                }
                if (granule == last) {
                    break;
                }
            }
        }
    }

    std::vector<Record> records;
    std::unique_ptr<std::atomic<std::uint32_t>[]> filter;
    std::atomic<std::uint32_t> wide_records{0};
};

} // namespace VideoCore
