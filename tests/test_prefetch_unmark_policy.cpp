// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cassert>
#include <vector>

#include "video_core/buffer_cache/prefetch_unmark_policy.h"

using VideoCore::AddressRange;
using VideoCore::UnmarksWaitingForNewerDownloads;

int main() {
    using V = std::vector<AddressRange>;
    constexpr std::uint64_t page = 0x1000;
    // Nothing in flight: unprotect right away.
    assert(UnmarksWaitingForNewerDownloads({{0x1000, 0x1800}}, {}, page).empty());
    // A newer download of the same bytes is in flight: wait, page aligned.
    assert((UnmarksWaitingForNewerDownloads({{0x1100, 0x1800}}, {{0x1200, 0x1300}}, page) ==
            V{{0x1000, 0x2000}}));
    // Same page, different bytes: protection is per page, so still wait.
    assert((UnmarksWaitingForNewerDownloads({{0x1000, 0x1100}}, {{0x1f00, 0x2000}}, page) ==
            V{{0x1000, 0x2000}}));
    // Neighbouring page only: no wait.
    assert(UnmarksWaitingForNewerDownloads({{0x1000, 0x2000}}, {{0x2000, 0x3000}}, page).empty());
    // Several unmarks, only the overlapped one waits, listed once.
    assert((UnmarksWaitingForNewerDownloads({{0x1000, 0x2000}, {0x5000, 0x5010}},
                                            {{0x5008, 0x6000}, {0x5000, 0x5004}}, page) ==
            V{{0x5000, 0x6000}}));
    return 0;
}
