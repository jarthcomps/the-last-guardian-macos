// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace Libraries::VideoOut {

/// SHADPS4_EARLY_VO_LABEL=1: release a display buffer's flip label when the next flip has been
/// prepared, instead of after the presenter thread shows that next flip.
///
/// A flip label stays 1 while its buffer is "on screen"; the game's graphics ring waits
/// (WaitRegMem) for it to return to 0 before rendering into that buffer again. The emulator
/// copies each flipped buffer into its own presentation frame when the flip is submitted
/// (Presenter::PrepareFrame records that copy in the same command stream as the game's later
/// rendering), so once the next flip is prepared nothing reads the previous buffer any more.
/// Releasing it then instead of after the next vblank slot and Present() takes the presenter's
/// pacing off the command processor's critical path. Flip events and vblank timing are unchanged.
struct EarlyLabelRelease {
    std::int32_t previous = -1;

    /// The flip of `index` was prepared; returns the buffer whose label can be cleared now, or -1.
    std::int32_t OnPrepared(std::int32_t index) {
        if (index < 0) {
            return -1; // blank frame: keeps the previous buffer on screen
        }
        const std::int32_t release = previous != index ? previous : -1;
        previous = index;
        return release;
    }
};

inline bool EarlyVoLabelEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("SHADPS4_EARLY_VO_LABEL");
        return value != nullptr && std::string_view{value} == "1";
    }();
    return enabled;
}

} // namespace Libraries::VideoOut
