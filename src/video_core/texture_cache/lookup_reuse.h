// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace VideoCore {
// Everything a shader image binding's lookup result depends on: the T# (after GetSharp), the
// ImageResource flags its ImageDesc reads (depth, array, written; the others are kept too), the
// mip fallback mode and index, the binding index inside a mip array, and the alt tile bit that
// ImageInfo takes from the T# and the Neo mode. The shader program is not an input: shaders that
// bind the same T# with the same flags resolve to the same image and view, so they share an entry.
struct TextureLookupKey {
    std::array<std::uint32_t, 8> descriptor{};
    std::uint32_t resource_flags{}, mip_mode{}, mip_index{}, binding_index{};
    bool alt_tile{};
    bool operator==(const TextureLookupKey&) const = default;
};

// Generation validation and result use must be serialized with image registration/removal.
// A result is valid for one image generation and one GC tick. The tick matters because
// ResolveOverlap removes an overlapping image only once it is old enough (safe_to_delete): a
// lookup made before that tick keeps the image, and the same lookup after it removes the image
// (which bumps the generation) and may resolve to another one. Without the tick, the cached
// result would still name the kept image.
template <typename Key, typename Result>
class LookupReuseEntry {
public:
    const Result* Find(const Key& requested, std::uint64_t current_generation,
                       std::uint64_t current_tick) const {
        return result && generation == current_generation && tick == current_tick &&
                       key == requested
                   ? &*result
                   : nullptr;
    }
    void Store(const Key& requested, std::uint64_t current_generation, std::uint64_t current_tick,
               Result resolved) {
        key = requested;
        generation = current_generation;
        tick = current_tick;
        result = std::move(resolved);
    }
    void Invalidate() { result.reset(); }
private:
    Key key{};
    std::uint64_t generation{};
    std::uint64_t tick{};
    std::optional<Result> result;
};
} // namespace VideoCore
