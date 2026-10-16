// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cassert>

#include "video_core/screenshot_burst.h"

using VideoCore::ScreenshotBurstCounter;

int main() {
    ScreenshotBurstCounter counter;
    assert(!counter.Next());
    assert(counter.Start(0) == 0);
    assert(!counter.Next());

    assert(counter.Start(3) == 1);
    assert(counter.Start(5) == 0); // busy while the first burst runs
    for (unsigned expected = 0; expected < 3; ++expected) {
        const auto frame = counter.Next();
        assert(frame && frame->burst_id == 1 && frame->index == expected);
    }
    assert(!counter.Next());

    assert(counter.Start(1) == 2);
    const auto frame = counter.Next();
    assert(frame && frame->burst_id == 2 && frame->index == 0);
    assert(!counter.Next());
    return 0;
}
