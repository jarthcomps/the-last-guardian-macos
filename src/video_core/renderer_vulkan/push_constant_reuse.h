// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <cstring>

namespace Vulkan {

/// Remembers only the last complete push call. Any different or unsupported call replaces or
/// invalidates it, including overlapping partial writes and writes to other shader stages.
/// This intentionally does not infer pipeline-layout compatibility or merge byte ranges.
template <typename Layout>
class PushConstantReuse {
public:
    bool CanSkip(Layout layout, std::uint32_t stages, std::uint32_t offset,
                 std::uint32_t size, const void* data) {
        if (!layout || !stages || !data || !size || size > bytes.size() ||
            offset % 4 || size % 4 || offset > UINT32_MAX - size) {
            Reset();
            return false;
        }
        if (valid && last_layout == layout && last_stages == stages && last_offset == offset &&
            last_size == size && std::memcmp(bytes.data(), data, size) == 0) {
            return true;
        }
        last_layout = layout;
        last_stages = stages;
        last_offset = offset;
        last_size = size;
        std::memcpy(bytes.data(), data, size);
        valid = true;
        return false;
    }

    void Reset() { valid = false; }

private:
    bool valid{};
    Layout last_layout{};
    std::uint32_t last_stages{}, last_offset{}, last_size{};
    std::array<std::uint8_t, 256> bytes{};
};

} // namespace Vulkan
