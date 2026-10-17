// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <limits>
#include "core/backing_range.h"

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); return 1; \
} } while (false)

int main() {
    using Core::BackingRange;
    using Core::ResolveBackingOffset;
    constexpr auto Max = std::numeric_limits<std::uint64_t>::max();
    const BackingRange first{0x100000000, 0x4000, 0x8000, true};
    CHECK(ResolveBackingOffset(first, 0x10000, 0x100000020, 8) == 0x8020);
    CHECK(ResolveBackingOffset(first, 0x10000, 0x100003ff8, 8) == 0xbff8);
    CHECK(!ResolveBackingOffset(first, 0x10000, 0x100003ff8, 9));
    CHECK(!ResolveBackingOffset(first, 0x10000, 0x0ffffffff, 8));
    CHECK(!ResolveBackingOffset(first, 0x10000, 0x100004000, 1));
    CHECK(!ResolveBackingOffset(first, 0x10000, 0x100000020, 0));
    CHECK(!ResolveBackingOffset(first, 0x10000, 0x100000020, Max));
    // Adjacent guest ranges may refer to completely different physical allocations.
    const BackingRange next{0x100004000, 0x4000, 0, true};
    CHECK(ResolveBackingOffset(next, 0x10000, 0x100004020, 8) == 0x20);
    // macOS executable mappings are anonymous, even when VMA metadata lists physical areas.
    CHECK(!ResolveBackingOffset({first.guest_base, first.size, first.physical_offset, false},
                                0x10000, first.guest_base, 8));
    CHECK(!ResolveBackingOffset({0x1000, 0x4000, 0x10000, true}, 0x10000, 0x1000, 8));
    CHECK(!ResolveBackingOffset({0x1000, 0x4000, 0xfffc, true}, 0x10000, 0x1000, 8));
    CHECK(!ResolveBackingOffset({0x1000, 0x4000, Max - 3, true}, Max, 0x1008, 1));
    CHECK(!ResolveBackingOffset({Max - 7, 8, 0, true}, 0x10000, Max - 3, 8));
    CHECK(ResolveBackingOffset({Max - 7, 8, 0, true}, 0x10000, Max - 3, 4) == 4);
    // Host-side internal shader pointers must never index the 40-bit guest tracker.
    constexpr std::uint64_t GuestLength = std::uint64_t{1} << 40;
    CHECK(Core::ContainsBackingRequest(0, GuestLength, 0x32aaf0000, 8));
    CHECK(!Core::ContainsBackingRequest(0, GuestLength, 0x7000019248ec, 8));
    CHECK(Core::ContainsBackingRequest(0, GuestLength, GuestLength - 8, 8));
    CHECK(!Core::ContainsBackingRequest(0, GuestLength, GuestLength - 8, 9));
    CHECK(!Core::ContainsBackingRequest(0, GuestLength, Max - 7, 8));
    return 0;
}
