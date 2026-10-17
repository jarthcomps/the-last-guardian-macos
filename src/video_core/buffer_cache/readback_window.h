// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

namespace VideoCore {

constexpr std::uint64_t ReadbackWindowSize(std::string_view value) {
    if (value == "64") return 64 * 1024;
    if (value == "128") return 128 * 1024;
    if (value == "256") return 256 * 1024;
    return 512 * 1024;
}

constexpr std::uint64_t ReadbackWindowEnd(std::uint64_t address, std::uint64_t size) {
    return address + std::min(size, std::numeric_limits<std::uint64_t>::max() - address);
}

// The requested bytes remain included even when they exceed the prefetch window.
// Buffer bounds limit speculative readback; page settlement remains the caller's job.
constexpr std::pair<std::uint64_t, std::uint64_t> CalculateReadbackWindow(
    std::uint64_t buffer_start, std::uint64_t buffer_end, std::uint64_t address,
    std::uint64_t size, std::uint64_t window_size) {
    const auto start = std::max(address & ~(window_size - 1), buffer_start);
    const auto end = std::min(std::max(ReadbackWindowEnd(start, window_size),
                                      ReadbackWindowEnd(address, size)), buffer_end);
    return {start, end};
}

} // namespace VideoCore
