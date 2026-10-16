// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>

#include "common/types.h"

namespace VideoCore {

struct GpuCopySynchronization {
    u32 source_size{};
    u32 destination_size{};
    bool source_is_texel_buffer{};
};

constexpr std::optional<GpuCopySynchronization> PlanGpuCopySynchronization(
    u32 byte_count, u32 source_texel_descriptor_size = 0) noexcept {
    if (byte_count == 0 ||
        (source_texel_descriptor_size != 0 && source_texel_descriptor_size < byte_count)) {
        return std::nullopt;
    }
    return GpuCopySynchronization{
        source_texel_descriptor_size != 0 ? source_texel_descriptor_size : byte_count,
        byte_count, source_texel_descriptor_size != 0,
    };
}

template <typename SourceSync, typename DestinationSync, typename InvalidateDestination>
void SynchronizeGpuCopyBindings(const GpuCopySynchronization& plan, SourceSync&& source_sync,
                                DestinationSync&& destination_sync,
                                InvalidateDestination&& invalidate_destination) {
    // Match formatted BindBuffers: materialize the image-backed source before preparing and
    // invalidating the destination. Image lookup uses the descriptor size, not the copied prefix.
    source_sync(plan.source_size, plan.source_is_texel_buffer);
    destination_sync(plan.destination_size);
    invalidate_destination(plan.destination_size);
}

constexpr bool CanCopyGpuBufferRanges(u64 source_offset, u64 source_size, u64 destination_offset,
                                     u64 destination_size, u32 byte_count,
                                     bool same_buffer) noexcept {
    if (byte_count == 0 || source_offset > source_size || destination_offset > destination_size ||
        byte_count > source_size - source_offset ||
        byte_count > destination_size - destination_offset) {
        return false;
    }
    return !same_buffer || source_offset >= destination_offset + byte_count ||
           destination_offset >= source_offset + byte_count;
}

} // namespace VideoCore
