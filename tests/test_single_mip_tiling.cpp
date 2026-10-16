// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include <initializer_list>
#include <cstdint>
#include "video_core/texture_cache/single_mip_tiling.h"

static void Check(bool condition) {
    if (!condition) {
        std::abort();
    }
}

int main() {
    using VideoCore::SingleMipTilingEnabled;
    using VideoCore::SingleMipTilingSafe;
    Check(!SingleMipTilingEnabled(nullptr));
    Check(!SingleMipTilingEnabled(""));
    Check(!SingleMipTilingEnabled("0"));
    Check(!SingleMipTilingEnabled("true"));
    Check(!SingleMipTilingEnabled("1 "));
    Check(SingleMipTilingEnabled("1"));
    for (const std::uint32_t bits : {8u, 16u, 32u, 64u, 128u}) {
        const auto bytes_per_pixel = bits / 8;
        for (std::uint64_t mip_texels = 1; mip_texels <= 1024; ++mip_texels) {
            for (std::uint64_t guest_texels = 1; guest_texels <= 1100; ++guest_texels) {
                const auto dispatched = (guest_texels / 64) * 64;
                const bool safe = SingleMipTilingSafe(guest_texels * bytes_per_pixel,
                                                      mip_texels * bytes_per_pixel, bits, 1);
                Check(safe == (dispatched != 0 && dispatched <= mip_texels));
                if (safe) {
                    // The original GetMipLevel loop cannot execute even for the final lane.
                    Check(dispatched - 1 < mip_texels);
                }
            }
        }
        Check(!SingleMipTilingSafe(256 * bytes_per_pixel, 64 * bytes_per_pixel, bits, 1));
        Check(!SingleMipTilingSafe(64 * bytes_per_pixel, 64 * bytes_per_pixel, bits, 2));
        Check(!SingleMipTilingSafe(64 * bytes_per_pixel, 64 * bytes_per_pixel, bits, 0));
    }
    Check(!SingleMipTilingSafe(0, 4096, 32, 1));
    Check(!SingleMipTilingSafe(4096, 0, 32, 1));
    Check(!SingleMipTilingSafe(4096, 4096, 0, 1));
    Check(!SingleMipTilingSafe(4096, 4096, 96, 1));
    Check(SingleMipTilingSafe(3'932'160, 3'932'160, 32, 1));
    // A partial final group preserves the original dispatch; crossing into padding does not.
    Check(SingleMipTilingSafe(260, 256, 32, 1));
    Check(!SingleMipTilingSafe(512, 256, 32, 1));
    Check(SingleMipTilingSafe(UINT64_MAX, UINT64_MAX, 8, 1));
}
