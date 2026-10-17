// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
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

// The caller serializes this registry with its readback mutex. It includes ordinary
// asynchronous copies as well as prefetches, regardless of page-unmark policy.
class PendingBackingCopies {
public:
    void Add(std::uint64_t id,
             std::span<const std::pair<std::uintptr_t, std::uintptr_t>> copies) {
        records.push_back({id, {copies.begin(), copies.end()}});
    }

    void Remove(std::uint64_t id) {
        std::erase_if(records, [id](const Record& record) { return record.id == id; });
    }

    bool Overlaps(std::uintptr_t address, std::uint64_t size) const {
        for (const auto& record : records) {
            if (ReadbackCopiesOverlap(record.copies, address, size)) {
                return true;
            }
        }
        return false;
    }

private:
    struct Record {
        std::uint64_t id;
        std::vector<std::pair<std::uintptr_t, std::uintptr_t>> copies;
    };
    std::vector<Record> records;
};

} // namespace VideoCore
