// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>

namespace DebugStateType {

/// Lock-free mirror of "the register dump map is not empty". The command processor asks this for
/// every draw and dispatch, so it must not take the mutex that guards the map. The writer calls
/// Set with the map's emptiness after every change to the map, while it still holds that mutex.
/// Waiting() is an acquire load, so it sees the state published by the latest Set.
class WaitingDumpFlag {
public:
    void Set(bool non_empty) noexcept {
        flag.store(non_empty, std::memory_order_release);
    }

    [[nodiscard]] bool Waiting() const noexcept {
        return flag.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> flag{};
};

} // namespace DebugStateType
