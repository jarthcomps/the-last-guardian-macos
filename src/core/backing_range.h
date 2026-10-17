// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <optional>

namespace Core {

struct BackingRange {
    std::uint64_t guest_base;
    std::uint64_t size;
    std::uint64_t physical_offset;
    bool shared;
};

// Use subtraction so malformed addresses and sizes cannot wrap past either mapping.
inline bool ContainsBackingRequest(std::uint64_t base, std::uint64_t length,
                                   std::uint64_t address, std::uint64_t size) {
    return size != 0 && address >= base && address - base < length &&
           size <= length - (address - base);
}

inline std::optional<std::uint64_t> ResolveBackingOffset(const BackingRange& range,
                                                        std::uint64_t backing_length,
                                                        std::uint64_t address,
                                                        std::uint64_t size) {
    if (!range.shared || !ContainsBackingRequest(range.guest_base, range.size, address, size) ||
        range.physical_offset >= backing_length) {
        return std::nullopt;
    }
    const auto relative = address - range.guest_base;
    const auto available = backing_length - range.physical_offset;
    if (relative >= available || size > available - relative) {
        return std::nullopt;
    }
    return range.physical_offset + relative;
}

} // namespace Core
