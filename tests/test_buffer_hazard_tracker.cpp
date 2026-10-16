// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cassert>

#include "video_core/buffer_cache/buffer_hazard_tracker.h"

using VideoCore::BufferHazardTracker;

int main() {
    BufferHazardTracker t;
    // Nothing recorded: no hazard.
    assert(!t.Hazard(1, 0, 100, true));

    // A write of [0, 100) in epoch 1.
    t.Commit(1, 0, 100, true);
    assert(t.Hazard(1, 50, 60, false));   // read after write, overlapping
    assert(t.Hazard(1, 99, 200, true));   // write after write, overlapping
    assert(!t.Hazard(1, 100, 200, false)); // disjoint read
    assert(!t.Hazard(1, 4096, 8192, true)); // disjoint write
    assert(!t.Hazard(2, 0, 100, false));  // a full barrier since then ordered it

    // Reads only block writes.
    t.Commit(1, 200, 300, false);
    assert(!t.Hazard(1, 250, 260, false)); // read after read
    assert(t.Hazard(1, 250, 260, true));   // write after read
    assert(!t.Hazard(1, 300, 400, true));  // touching but not overlapping

    // A new epoch drops everything recorded before.
    t.Commit(2, 1000, 1100, false);
    assert(!t.Hazard(2, 0, 100, false));
    assert(!t.Hazard(2, 200, 300, true));
    assert(t.Hazard(2, 1050, 1060, true));

    // Touching ranges merge; more than MaxRanges disjoint ranges stay conservative.
    BufferHazardTracker m;
    for (std::uint64_t i = 0; i < BufferHazardTracker::MaxRanges + 4; ++i) {
        m.Commit(5, i * 1000, i * 1000 + 10, true);
    }
    for (std::uint64_t i = 0; i < BufferHazardTracker::MaxRanges + 4; ++i) {
        assert(m.Hazard(5, i * 1000 + 5, i * 1000 + 6, false)); // every write still seen
    }
    assert(!m.Hazard(5, 500, 600, false)); // gap between early ranges is still free
    return 0;
}
