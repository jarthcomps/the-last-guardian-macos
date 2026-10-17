// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstdio>
#include <limits>
#include "video_core/buffer_cache/download_overlap.h"

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); return 1; \
} } while (false)

int main() {
    using VideoCore::ReadbackCopiesOverlap;
    using Range = std::pair<std::uintptr_t, std::uintptr_t>;
    const std::array copies{Range{0x1800, 0x1900}, Range{0x1c00, 0x1c40}};
    // A pending page-sized download does not change a shader header outside its actual copies.
    CHECK(!ReadbackCopiesOverlap(copies, 0x1010, 8));
    CHECK(ReadbackCopiesOverlap(copies, 0x1800, 8));
    CHECK(ReadbackCopiesOverlap(copies, 0x17fc, 8));
    CHECK(!ReadbackCopiesOverlap(copies, 0x17f8, 8));
    CHECK(ReadbackCopiesOverlap(copies, 0x18ff, 1));
    CHECK(!ReadbackCopiesOverlap(copies, 0x1900, 8));
    CHECK(ReadbackCopiesOverlap(copies, 0x1c20, 8));
    CHECK(!ReadbackCopiesOverlap(copies, 0x1c40, 8));
    CHECK(ReadbackCopiesOverlap(copies, 0x1700, 0x600));
    CHECK(!ReadbackCopiesOverlap(copies, 0x1800, 0));
    CHECK(!ReadbackCopiesOverlap({}, 0x1800, 8));
    const std::array invalid{Range{0x1900, 0x1800}, Range{0x1800, 0x1800}};
    CHECK(!ReadbackCopiesOverlap(invalid, 0x1800, 8));
    constexpr auto Max = std::numeric_limits<std::uint64_t>::max();
    CHECK(!ReadbackCopiesOverlap(copies, Max - 3, 8));
    VideoCore::PendingBackingCopies pending;
    CHECK(!pending.Overlaps(0x1800, 8));
    pending.Add(1, copies); // Ordinary asynchronous copy, with no deferred page unmark.
    CHECK(pending.Overlaps(0x1800, 8));
    CHECK(!pending.Overlaps(0x1010, 8));
    pending.Add(2, copies); // A newer prefetch of the same bytes must keep them stale.
    pending.Remove(1);
    CHECK(pending.Overlaps(0x1800, 8));
    pending.Remove(999);
    CHECK(pending.Overlaps(0x1800, 8));
    pending.Remove(2);
    CHECK(!pending.Overlaps(0x1800, 8));
    // Copy metadata must survive the caller reusing its temporary list.
    std::array temporary{Range{0x2000, 0x2010}};
    pending.Add(3, temporary);
    temporary[0] = Range{0x3000, 0x3010};
    CHECK(pending.Overlaps(0x2000, 8));
    CHECK(!pending.Overlaps(0x3000, 8));
    pending.Remove(3);
    CHECK(!pending.Overlaps(0x2000, 8));
    return 0;
}
