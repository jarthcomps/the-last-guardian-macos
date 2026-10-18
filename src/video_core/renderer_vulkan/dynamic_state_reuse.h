// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Vulkan {
class DynamicStateReuse {
public:
    enum class Key : size_t {
        Viewport, Scissor, DepthTest, DepthWrite, DepthCompare, DepthBoundsTest,
        DepthBounds, DepthBiasEnable, DepthBias, StencilTest, StencilOp,
        StencilReference, StencilWriteMask, StencilCompareMask, PrimitiveRestart,
        RasterizerDiscard, Cull, FrontFace, LineWidth, BlendConstants, ColorWriteMask,
        FeedbackLoop, Count
    };
    // A single entry per state is conservative for partial/overlapping array and face updates:
    // only an exact repeat of the last setter can be suppressed.
    bool ShouldRecord(Key key, uint32_t variant, uint32_t first, uint32_t count,
                      const void* values, size_t size) {
        auto& entry = entries[static_cast<size_t>(key)];
        if (!values || size == 0 || size > entry.bytes.size()) {
            entry.valid = false;
            return true;
        }
        if (entry.valid && entry.variant == variant && entry.first == first &&
            entry.count == count && entry.size == size &&
            std::memcmp(entry.bytes.data(), values, size) == 0) {
            return false;
        }
        entry.valid = true;
        entry.variant = variant;
        entry.first = first;
        entry.count = count;
        entry.size = size;
        std::memcpy(entry.bytes.data(), values, size);
        return true;
    }
    void Reset() {
        for (auto& entry : entries) entry.valid = false;
    }
private:
    struct Entry {
        std::array<std::byte, 512> bytes{};
        size_t size{};
        uint32_t variant{}, first{}, count{};
        bool valid{};
    };
    std::array<Entry, static_cast<size_t>(Key::Count)> entries{};
};
} // namespace Vulkan
