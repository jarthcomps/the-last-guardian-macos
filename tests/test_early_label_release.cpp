// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cassert>

#include "core/libraries/videoout/early_label_release.h"

using Libraries::VideoOut::EarlyLabelRelease;

int main() {
    EarlyLabelRelease r;
    assert(r.OnPrepared(0) == -1); // first flip: nothing on screen before it
    assert(r.OnPrepared(1) == 0);  // buffer 0 is free once buffer 1's flip is prepared
    assert(r.OnPrepared(2) == 1);
    assert(r.OnPrepared(-1) == -1); // blank frame releases nothing and keeps 2 as previous
    assert(r.OnPrepared(0) == 2);
    assert(r.OnPrepared(0) == -1); // re-flipping the same buffer never releases it
    assert(r.OnPrepared(1) == 0);
    assert(!Libraries::VideoOut::EarlyVoLabelEnabled()); // SHADPS4_EARLY_VO_LABEL unset
    return 0;
}
