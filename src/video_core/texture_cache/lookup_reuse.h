// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace VideoCore {
struct TextureLookupKey {
    std::array<std::uint32_t, 8> descriptor{};
    const void* specialization{};
    std::uint64_t program{};
    std::uint32_t resource_flags{}, mip_mode{}, mip_index{}, binding_index{};
    bool alt_tile{};
    bool operator==(const TextureLookupKey&) const = default;
};

// Generation validation and result use must be serialized with image registration/removal.
template <typename Key, typename Result>
class LookupReuseEntry {
public:
    const Result* Find(const Key& requested, std::uint64_t current_generation) const {
        return result && generation == current_generation && key == requested ? &*result : nullptr;
    }
    void Store(const Key& requested, std::uint64_t current_generation, Result resolved) {
        key = requested;
        generation = current_generation;
        result = std::move(resolved);
    }
    void Invalidate() { result.reset(); }
private:
    Key key{};
    std::uint64_t generation{};
    std::optional<Result> result;
};
} // namespace VideoCore
