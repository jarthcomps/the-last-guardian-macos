// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <vector>

namespace VideoCore {

/// Half-open guest address range [begin, end).
struct AddressRange {
    std::uint64_t begin{};
    std::uint64_t end{};
    bool operator==(const AddressRange&) const = default;
};

/// SHADPS4_PREFETCH_STRICT_UNMARK: the page-aligned ranges of landed downloads (`unmarks`) that a
/// newer download still in flight overlaps. Their pages must not be unprotected before that newer
/// download has landed, or the game reads the older data (stale feather simulation state) or has
/// its own writes overwritten when the newer download lands.
inline std::vector<AddressRange> UnmarksWaitingForNewerDownloads(
    const std::vector<AddressRange>& unmarks, const std::vector<AddressRange>& in_flight,
    std::uint64_t page_size) {
    std::vector<AddressRange> waits;
    for (const AddressRange& unmark : unmarks) {
        const std::uint64_t begin = unmark.begin / page_size * page_size;
        const std::uint64_t end = (unmark.end + page_size - 1) / page_size * page_size;
        for (const AddressRange& download : in_flight) {
            if (download.begin < end && begin < download.end) {
                waits.push_back({begin, end});
                break;
            }
        }
    }
    return waits;
}

} // namespace VideoCore
