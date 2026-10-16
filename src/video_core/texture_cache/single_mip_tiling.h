// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string_view>

namespace VideoCore {

inline bool SingleMipTilingEnabled(const char* value) {
    return value != nullptr && std::string_view{value} == "1";
}

/// Prove that every invocation of the existing 64-thread dispatch remains in mip zero.
/// The generic shader may select another mip in padding, so one mip alone is insufficient.
constexpr bool SingleMipTilingSafe(std::uint64_t guest_bytes, std::uint64_t first_mip_bytes,
                                  std::uint32_t bits_per_pixel, std::uint32_t mip_count) {
    if (mip_count != 1 || (bits_per_pixel != 8 && bits_per_pixel != 16 &&
                          bits_per_pixel != 32 && bits_per_pixel != 64 &&
                          bits_per_pixel != 128)) {
        return false;
    }
    const auto bytes_per_pixel = bits_per_pixel / 8;
    const auto dispatched_texels = (guest_bytes / bytes_per_pixel / 64) * 64;
    return dispatched_texels != 0 && dispatched_texels <= first_mip_bytes / bytes_per_pixel;
}

} // namespace VideoCore
