// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cassert>
#include <string>

#include "core/libraries/kernel/wait_stats.h"

int main() {
    Libraries::Kernel::WaitStats stats;
    assert(stats.TakeSummary(100).empty()); // first call only starts the interval
    stats.Record("eq:render filter=-13 ident=0", 5'000'000);
    stats.Record("eq:render filter=-13 ident=0", 7'000'000);
    stats.Record("sem:frame", 2'500'000);
    assert(stats.TakeSummary(100 + 999'999'999).empty()); // interval not over
    const std::string s = stats.TakeSummary(100 + 1'000'000'000ULL);
    assert(s == "WAIT_STATS eq:render filter=-13 ident=0 n=2 ms=12\n"
                "WAIT_STATS sem:frame n=1 ms=2\n");
    assert(stats.TakeSummary(100 + 2'000'000'000ULL).empty()); // cleared, nothing new
    return 0;
}
