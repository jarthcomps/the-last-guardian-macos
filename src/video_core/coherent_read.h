// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace VideoCore {

// The physical alias avoids a page fault only when these exact bytes are current.
// The backing lookup separately proves that the mapping is actually shared.
template <typename IsCurrent, typename FindBacking>
const void* CoherentReadPointer(std::uintptr_t address, std::uint64_t size, bool enabled,
                                IsCurrent&& is_current, FindBacking&& find_backing) {
    if (enabled && size != 0 && is_current(address, size)) {
        if (const void* backing = find_backing(address, size)) {
            return backing;
        }
    }
    return reinterpret_cast<const void*>(address);
}

} // namespace VideoCore
