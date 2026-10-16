// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

#include "common/types.h"

namespace Vulkan {

constexpr bool A912CopyShaderHleEnabled(const char* enable_flag,
                                       const char* disable_flag) noexcept {
    return enable_flag != nullptr && std::string_view{enable_flag} == "1" &&
           (disable_flag == nullptr || std::string_view{disable_flag} != "1");
}

constexpr bool A912DiagnosticsEnabled(const char* flag) noexcept {
    return flag != nullptr && std::string_view{flag} == "1";
}

struct A912CopyAliases {
    bool source_metadata{};
    bool destination_metadata{};
    bool source_image{};
    bool destination_image{};
    bool source_is_formatted{};
    bool source_image_size_matches{};
    u32 source_size{};
};

inline constexpr u32 A912CopyStreamThreshold = 16 * 1024;

constexpr bool A912ImageSourceCopyEnabled(const char* flag) noexcept {
    return flag != nullptr && std::string_view{flag} == "1";
}

constexpr bool A912CopyAliasesCompatible(const A912CopyAliases& aliases,
                                        bool image_source_enabled) noexcept {
    if (aliases.source_metadata || aliases.destination_metadata || aliases.destination_image) {
        return false;
    }
    if (!aliases.source_image) {
        return true;
    }
    // Small read-only descriptors can take ObtainBuffer's CPU stream-copy path, which does
    // not synchronize from an image. Only extend the path that originally did synchronize.
    return aliases.source_is_formatted && aliases.source_image_size_matches &&
           aliases.source_size > A912CopyStreamThreshold && image_source_enabled;
}

constexpr bool A912DiagnosticReportAllowed(bool enabled, u32 reports_so_far) noexcept {
    return enabled && reports_so_far < 8;
}

struct A912CopyDescriptorFormat {
    u32 data_format{};
    u32 number_format{};
    u32 stride{};
    std::array<u32, 4> dst_select{};
    u32 number_conversion{};
    bool swizzle_enable{};
    bool add_tid_enable{};
    bool cache_swizzle{};
};

struct A912CopyResourceCompatibilityInput {
    u32 source_buffer_type{};
    u32 destination_buffer_type{};
    u32 source_used_types{};
    u32 destination_used_types{};
    bool source_is_written{};
    bool destination_is_written{};
    bool source_is_formatted{};
    bool destination_is_formatted{};
    bool source_post_op_is_none{};
    bool destination_post_op_is_none{};
    bool formatted_descriptors_are_compatible{};
};

inline constexpr u32 A912GuestBufferType = 0;
inline constexpr u32 A912F32Type = 1 << 12;
inline constexpr u32 A912FormattedTypes = (1 << 7) | (1 << 8) | (1 << 9) | (1 << 12);
inline constexpr u32 A912Format32 = 4;
inline constexpr u32 A912NumberFormatUint = 4;
inline constexpr u32 A912NumberConversionNone = 0;
inline constexpr u32 A912SwizzleZero = 0;
inline constexpr u32 A912SwizzleRed = 4;

constexpr bool A912CopyResourcesCompatible(
    const A912CopyResourceCompatibilityInput& input) noexcept {
    if (input.source_buffer_type != A912GuestBufferType ||
        input.destination_buffer_type != A912GuestBufferType || input.source_is_written ||
        !input.destination_is_written || !input.source_post_op_is_none ||
        !input.destination_post_op_is_none) {
        return false;
    }

    const bool unformatted_f32 =
        !input.source_is_formatted && !input.destination_is_formatted &&
        input.source_used_types == A912F32Type && input.destination_used_types == A912F32Type;
    const bool formatted_scalar_uint =
        input.source_is_formatted && input.destination_is_formatted &&
        input.source_used_types == A912FormattedTypes &&
        input.destination_used_types == A912FormattedTypes &&
        input.formatted_descriptors_are_compatible;
    return unformatted_f32 || formatted_scalar_uint;
}

constexpr bool IsA912ScalarUintFormat(const A912CopyDescriptorFormat& descriptor) noexcept {
    // Scalar R32_UINT descriptors with identity-R/zero-GBA selection are bit-preserving
    // for this shader's one-component formatted F32 load/store sequence.
    return descriptor.data_format == A912Format32 &&
           descriptor.number_format == A912NumberFormatUint &&
           descriptor.stride == sizeof(u32) &&
           descriptor.dst_select == std::array<u32, 4>{A912SwizzleRed, A912SwizzleZero,
                                                       A912SwizzleZero, A912SwizzleZero} &&
           descriptor.number_conversion == A912NumberConversionNone && !descriptor.swizzle_enable &&
           !descriptor.add_tid_enable && !descriptor.cache_swizzle;
}

constexpr bool AreA912ScalarUintFormatsMatching(const A912CopyDescriptorFormat& source,
                                                const A912CopyDescriptorFormat& destination) noexcept {
    return IsA912ScalarUintFormat(source) && IsA912ScalarUintFormat(destination) &&
           source.data_format == destination.data_format &&
           source.number_format == destination.number_format && source.stride == destination.stride &&
           source.dst_select == destination.dst_select &&
           source.number_conversion == destination.number_conversion;
}

/// Pure bounds/shape guard for the cached 0xa912f7d4 float-copy compute shader.
struct A912CopyPlanInput {
    u64 shader_hash{};
    u32 dispatch_x{};
    u32 dispatch_y{};
    u32 dispatch_z{};
    u32 start_x{};
    u32 start_y{};
    u32 start_z{};
    u32 user_data_6{};
    u32 workgroup_size_x{};
    u32 workgroup_size_y{};
    u32 workgroup_size_z{};
    u32 partial_x{};
    u32 partial_y{};
    u32 partial_z{};
    u32 source_size{};
    u32 destination_size{};
    VAddr source_address{};
    VAddr destination_address{};
    u32 buffer_count{};
    bool source_is_guest_read_only{};
    bool destination_is_guest_written{};
    bool buffers_are_copy_compatible{};
    bool descriptors_are_untransformed{};
    bool has_texture_aliases{};
    bool has_other_resources{};
    bool has_user_data_6{};
};

enum class A912CopyPlanKind {
    Fallback,
    NoOp,
    Copy,
};

struct A912CopyPlan {
    A912CopyPlanKind kind{A912CopyPlanKind::Fallback};
    VAddr source_address{};
    VAddr destination_address{};
    u32 byte_count{};
};

inline bool CopyByteRangesOverlap(u64 first_offset, u64 first_size, u64 second_offset,
                                  u64 second_size) noexcept {
    return first_offset < second_offset + second_size &&
           second_offset < first_offset + first_size;
}

inline A912CopyPlan MakeA912CopyPlan(const A912CopyPlanInput& input) noexcept {
    static constexpr u64 ShaderHash = 0xa912f7d4;
    static constexpr u64 MaxGuestAddress = u64{1} << 40;
    static constexpr u64 WorkgroupSizeX = 64;

    if (input.shader_hash != ShaderHash || input.buffer_count != 2 || input.has_other_resources ||
        input.has_texture_aliases ||
        !input.source_is_guest_read_only || !input.destination_is_guest_written ||
        !input.buffers_are_copy_compatible || !input.descriptors_are_untransformed ||
        !input.has_user_data_6 ||
        input.workgroup_size_x != WorkgroupSizeX || input.workgroup_size_y != 1 ||
        input.workgroup_size_z != 1 || input.dispatch_y != 1 || input.dispatch_z != 1 ||
        input.start_x != 0 || input.start_y != 0 || input.start_z != 0 ||
        input.source_address == 0 || input.destination_address == 0) {
        return {};
    }

    if (input.partial_x != 0 || input.partial_y != 0 || input.partial_z != 0) {
        return {};
    }

    const u64 source_end = input.source_address + input.source_size;
    const u64 destination_end = input.destination_address + input.destination_size;
    if (input.source_size == 0 || input.destination_size == 0 ||
        source_end < input.source_address || destination_end < input.destination_address ||
        source_end > MaxGuestAddress || destination_end > MaxGuestAddress ||
        (input.source_address & (sizeof(u32) - 1)) != 0 ||
        (input.destination_address & (sizeof(u32) - 1)) != 0 ||
        (input.source_address != input.destination_address &&
         CopyByteRangesOverlap(input.source_address, input.source_size, input.destination_address,
                               input.destination_size))) {
        return {};
    }

    if ((input.source_size & 3) != 0 || (input.destination_size & 3) != 0 ||
        input.user_data_6 > input.source_size / sizeof(u32) ||
        input.user_data_6 > input.destination_size / sizeof(u32)) {
        return {};
    }

    const u64 dispatched_elements = static_cast<u64>(input.dispatch_x) * WorkgroupSizeX;
    const u64 elements = std::min<u64>(dispatched_elements, input.user_data_6);
    if (elements == 0 || input.source_address == input.destination_address) {
        return {A912CopyPlanKind::NoOp, input.source_address, input.destination_address, 0};
    }

    const u64 byte_count = elements * sizeof(u32);
    if (byte_count > std::numeric_limits<u32>::max()) {
        return {};
    }
    return {A912CopyPlanKind::Copy, input.source_address, input.destination_address,
            static_cast<u32>(byte_count)};
}

} // namespace Vulkan
