// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstring>
#include <tuple>
#include <type_traits>

#include "common/types.h"
#include "video_core/renderer_vulkan/bytes_equal.h"

namespace Vulkan {

/// Bytes of the register inputs of one render target lookup (the parts, back to back).
template <typename... Ts>
inline constexpr u32 TargetInputsSize = static_cast<u32>((std::size_t{0} + ... + sizeof(Ts)));

/// True when the stored inputs hold the same bytes as the parts laid out back to back.
template <typename... Ts>
bool TargetInputsEqual(const u8* stored, const Ts&... parts) noexcept {
    std::size_t offset = 0;
    bool equal = true;
    const auto same_part = [&](const auto& part) {
        equal = equal && BytesEqual(stored + offset, reinterpret_cast<const u8*>(&part),
                                    sizeof(part));
        offset += sizeof(part);
    };
    (same_part(parts), ...);
    return equal;
}

/// Stores the parts back to back.
template <typename... Ts>
void StoreTargetInputs(u8* stored, const Ts&... parts) noexcept {
    std::size_t offset = 0;
    ((std::memcpy(stored + offset, &parts, sizeof(Ts)), offset += sizeof(Ts)), ...);
}

/// Decides whether a lookup with these register inputs can reuse the entry's cached result. When
/// it cannot, the inputs are stored for the next lookup and the entry is left invalid: the caller
/// then does a full lookup and marks the entry valid with the current generation. `generation()`
/// is called only when the entry is valid and the size matches (short-circuit evaluation).
template <typename Cache, typename GenerationFn, typename... Ts>
bool MatchTargetInputs(Cache& cache, GenerationFn&& generation, const Ts&... parts) noexcept {
    constexpr u32 size = TargetInputsSize<Ts...>;
    static_assert(size <= std::tuple_size_v<std::remove_cvref_t<decltype(cache.inputs)>>,
                  "register inputs do not fit in the cache entry");
    const bool match = cache.valid && cache.size == size && cache.generation == generation() &&
                       TargetInputsEqual(cache.inputs.data(), parts...);
    if (!match) {
        cache.valid = false;
        cache.size = size;
        StoreTargetInputs(cache.inputs.data(), parts...);
    }
    return match;
}

} // namespace Vulkan
