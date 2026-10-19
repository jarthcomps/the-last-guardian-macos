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

    // History starts off and is switched on once.
    BufferHazardTracker h;
    assert(!h.HasHistory());
    h.StartHistory();
    assert(h.HasHistory());

    // Draws re-reading the same range merge into the latest slot instead of filling the table,
    // so later disjoint writes keep their own ranges.
    BufferHazardTracker r;
    for (int i = 0; i < 1000; ++i) {
        r.Commit(7, 0, 64, false);
    }
    for (std::uint64_t i = 1; i < BufferHazardTracker::MaxRanges; ++i) {
        r.Commit(7, i * 1000, i * 1000 + 10, true);
    }
    assert(!r.Hazard(7, 500, 600, true)); // no widening happened
    assert(r.Hazard(7, 10, 20, true));    // the read is still seen by a write

    // A range touching one older than the merge window gets its own slot; both still count.
    BufferHazardTracker w;
    w.Commit(9, 0, 100, true);
    for (std::uint64_t i = 1; i <= BufferHazardTracker::MergeWindow; ++i) {
        w.Commit(9, i * 1000, i * 1000 + 10, true);
    }
    w.Commit(9, 100, 200, true);
    assert(w.Hazard(9, 50, 51, false));
    assert(w.Hazard(9, 150, 151, false));
    assert(!w.Hazard(9, 300, 400, false));
    return 0;
}
