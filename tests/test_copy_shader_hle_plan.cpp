// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <limits>
#include <vector>
#include "video_core/buffer_cache/gpu_copy.h"
#include "video_core/renderer_vulkan/copy_shader_hle_plan.h"

using Vulkan::A912CopyPlanInput;
using Vulkan::A912CopyPlanKind;
using Vulkan::A912CopyDescriptorFormat;
using Vulkan::A912CopyResourceCompatibilityInput;
using Vulkan::A912CopyResourcesCompatible;
using Vulkan::A912DiagnosticReportAllowed;
using Vulkan::A912DiagnosticsEnabled;
using Vulkan::AreA912ScalarUintFormatsMatching;
using Vulkan::CopyByteRangesOverlap;
using Vulkan::MakeA912CopyPlan;

static A912CopyDescriptorFormat ValidScalarUintFormat() {
    return {
        .data_format = 4, // Format32
        .number_format = 4, // Uint
        .stride = sizeof(u32),
        .dst_select = {4, 0, 0, 0}, // Red, Zero, Zero, Zero
        .number_conversion = 0,
    };
}

static A912CopyResourceCompatibilityInput ValidFormattedResources() {
    return {
        .source_buffer_type = 0, // Guest
        .destination_buffer_type = 0,
        .source_used_types = 0x1380, // U8 | U16 | U32 | F32
        .destination_used_types = 0x1380,
        .source_is_written = false,
        .destination_is_written = true,
        .source_is_formatted = true,
        .destination_is_formatted = true,
        .source_post_op_is_none = true,
        .destination_post_op_is_none = true,
        .formatted_descriptors_are_compatible = true,
    };
}

static A912CopyPlanInput ValidInput() {
    return {
        .shader_hash = 0xa912f7d4,
        .dispatch_x = 2,
        .dispatch_y = 1,
        .dispatch_z = 1,
        .user_data_6 = 80,
        .workgroup_size_x = 64,
        .workgroup_size_y = 1,
        .workgroup_size_z = 1,
        .source_size = 512,
        .destination_size = 512,
        .source_address = 0x10000,
        .destination_address = 0x20000,
        .buffer_count = 2,
        .source_is_guest_read_only = true,
        .destination_is_guest_written = true,
        .buffers_are_copy_compatible = true,
        .descriptors_are_untransformed = true,
        .has_other_resources = false,
        .has_user_data_6 = true,
    };
}

int main() {
    using Vulkan::A912CopyAliases;
    using Vulkan::A912CopyAliasesCompatible;
    using Vulkan::A912ImageSourceCopyEnabled;
    if (A912ImageSourceCopyEnabled(nullptr) || A912ImageSourceCopyEnabled("") ||
        A912ImageSourceCopyEnabled("true") || A912ImageSourceCopyEnabled("0") ||
        !A912ImageSourceCopyEnabled("1")) return 57;
    A912CopyAliases aliases{
        .source_image = true,
        .source_is_formatted = true,
        .source_image_size_matches = true,
        .source_size = 98304,
    };
    if (A912CopyAliasesCompatible(aliases, false) ||
        !A912CopyAliasesCompatible(aliases, true)) return 58;
    for (int unsafe = 0; unsafe < 6; ++unsafe) {
        auto changed = aliases;
        switch (unsafe) {
        case 0: changed.source_metadata = true; break;
        case 1: changed.destination_metadata = true; break;
        case 2: changed.destination_image = true; break;
        case 3: changed.source_is_formatted = false; break;
        case 4: changed.source_size = Vulkan::A912CopyStreamThreshold; break;
        case 5: changed.source_image_size_matches = false; break;
        }
        if (A912CopyAliasesCompatible(changed, true)) return 59;
    }
    if (!A912CopyAliasesCompatible({}, false)) return 60;

    using VideoCore::PlanGpuCopySynchronization;
    using VideoCore::SynchronizeGpuCopyBindings;
    if (PlanGpuCopySynchronization(0) || PlanGpuCopySynchronization(320, 319)) return 61;
    const auto raw_copy = PlanGpuCopySynchronization(320);
    const auto formatted_copy = PlanGpuCopySynchronization(320, 98304);
    if (!raw_copy || raw_copy->source_size != 320 || raw_copy->destination_size != 320 ||
        raw_copy->source_is_texel_buffer || !formatted_copy ||
        formatted_copy->source_size != 98304 || formatted_copy->destination_size != 320 ||
        !formatted_copy->source_is_texel_buffer) return 62;

    // Exercise the same binding order used by CopyBufferGpu: materialize image bytes over the
    // full descriptor, then prepare and invalidate the copied destination prefix.
    std::vector<int> order;
    bool source_materialized = false;
    bool destination_prepared = false;
    bool arguments_match = true;
    SynchronizeGpuCopyBindings(
        *formatted_copy,
        [&](u32 size, bool texel) {
            arguments_match &= size == 98304 && texel;
            source_materialized = true;
            order.push_back(1);
        },
        [&](u32 size) {
            arguments_match &= source_materialized && size == 320;
            destination_prepared = true;
            order.push_back(2);
        },
        [&](u32 size) {
            arguments_match &= source_materialized && destination_prepared && size == 320;
            order.push_back(3);
        });
    if (!arguments_match || order != std::vector<int>{1, 2, 3}) return 63;

    if (A912DiagnosticsEnabled(nullptr) || A912DiagnosticsEnabled("") ||
        A912DiagnosticsEnabled("0") || A912DiagnosticsEnabled("true") ||
        !A912DiagnosticsEnabled("1")) {
        return 35;
    }
    if (A912DiagnosticReportAllowed(false, 0) || !A912DiagnosticReportAllowed(true, 0) ||
        !A912DiagnosticReportAllowed(true, 7) || A912DiagnosticReportAllowed(true, 8) ||
        A912DiagnosticReportAllowed(false, 7)) {
        return 36;
    }

    auto source_format = ValidScalarUintFormat();
    auto destination_format = ValidScalarUintFormat();
    if (!AreA912ScalarUintFormatsMatching(source_format, destination_format)) return 37;
    auto formatted_input = ValidInput();
    formatted_input.buffers_are_copy_compatible =
        AreA912ScalarUintFormatsMatching(source_format, destination_format);
    auto formatted_plan = MakeA912CopyPlan(formatted_input);
    if (formatted_plan.kind != A912CopyPlanKind::Copy || formatted_plan.byte_count != 320) {
        return 38;
    }

    auto resources = ValidFormattedResources();
    if (!A912CopyResourcesCompatible(resources)) return 43;
    resources = ValidFormattedResources();
    resources.destination_is_formatted = false;
    if (A912CopyResourcesCompatible(resources)) return 44;
    resources = ValidFormattedResources();
    resources.source_used_types = 0x1000; // F32-only mask must not pass formatted path
    if (A912CopyResourcesCompatible(resources)) return 45;
    resources = ValidFormattedResources();
    resources.destination_used_types = 0x1381; // Extra type bit must reject
    if (A912CopyResourcesCompatible(resources)) return 46;
    resources = ValidFormattedResources();
    resources.formatted_descriptors_are_compatible = false;
    if (A912CopyResourcesCompatible(resources)) return 47;
    resources = ValidFormattedResources();
    resources.source_buffer_type = 1; // Flatbuf
    if (A912CopyResourcesCompatible(resources)) return 48;
    resources = ValidFormattedResources();
    resources.source_is_written = true;
    if (A912CopyResourcesCompatible(resources)) return 49;
    resources = ValidFormattedResources();
    resources.destination_is_written = false;
    if (A912CopyResourcesCompatible(resources)) return 50;
    resources = ValidFormattedResources();
    resources.source_post_op_is_none = false;
    if (A912CopyResourcesCompatible(resources)) return 51;

    // The original unformatted F32 route remains supported by the same runtime predicate.
    resources = ValidFormattedResources();
    resources.source_is_formatted = false;
    resources.destination_is_formatted = false;
    resources.source_used_types = 0x1000;
    resources.destination_used_types = 0x1000;
    resources.formatted_descriptors_are_compatible = false;
    if (!A912CopyResourcesCompatible(resources)) return 52;

    // Format, conversion, component, stride, or address-transform changes make the
    // formatted load/store cease to be a guaranteed dword bit copy.
    const std::array<void (*)(A912CopyDescriptorFormat&), 8> unsafe_formats{
        [](auto& v) { v.number_format = 0; }, // Unorm
        [](auto& v) { v.number_format = 5; }, // Sint
        [](auto& v) { v.number_conversion = 1; },
        [](auto& v) { v.stride = 8; },
        [](auto& v) { v.dst_select = {7, 0, 0, 0}; },
        [](auto& v) { v.swizzle_enable = true; },
        [](auto& v) { v.add_tid_enable = true; },
        [](auto& v) { v.cache_swizzle = true; },
    };
    for (const auto fail : unsafe_formats) {
        source_format = ValidScalarUintFormat();
        destination_format = ValidScalarUintFormat();
        fail(source_format);
        if (AreA912ScalarUintFormatsMatching(source_format, destination_format)) return 39;
        source_format = ValidScalarUintFormat();
        fail(destination_format);
        if (AreA912ScalarUintFormatsMatching(source_format, destination_format)) return 40;
    }
    source_format = ValidScalarUintFormat();
    destination_format = ValidScalarUintFormat();
    destination_format.data_format = 11; // Format32_32
    if (AreA912ScalarUintFormatsMatching(source_format, destination_format)) return 41;

    // Image aliases remain a fallback even for otherwise compatible descriptors.
    formatted_input = ValidInput();
    formatted_input.has_texture_aliases = true;
    formatted_input.buffers_are_copy_compatible =
        AreA912ScalarUintFormatsMatching(ValidScalarUintFormat(), ValidScalarUintFormat());
    if (MakeA912CopyPlan(formatted_input).kind != A912CopyPlanKind::Fallback) return 42;

    if (Vulkan::A912CopyShaderHleEnabled(nullptr, nullptr) ||
        Vulkan::A912CopyShaderHleEnabled(nullptr, "1") ||
        Vulkan::A912CopyShaderHleEnabled("", nullptr) ||
        Vulkan::A912CopyShaderHleEnabled("0", nullptr) ||
        Vulkan::A912CopyShaderHleEnabled("true", nullptr) ||
        !Vulkan::A912CopyShaderHleEnabled("1", nullptr) ||
        Vulkan::A912CopyShaderHleEnabled("1", "1") ||
        !Vulkan::A912CopyShaderHleEnabled("1", "0")) {
        return 34;
    }
    if (!CopyByteRangesOverlap(0, 8, 4, 8) || !CopyByteRangesOverlap(4, 8, 0, 8) ||
        !CopyByteRangesOverlap(0, 16, 4, 4) || CopyByteRangesOverlap(0, 8, 8, 8) ||
        CopyByteRangesOverlap(0, 8, 9, 8)) {
        return 21;
    }

    auto input = ValidInput();
    auto plan = MakeA912CopyPlan(input);
    if (plan.kind != A912CopyPlanKind::Copy || plan.byte_count != 320 ||
        plan.source_address != input.source_address ||
        plan.destination_address != input.destination_address) {
        return 1;
    }

    // Dispatch bounds and the shader's user-data bound both cap the final partial group.
    input = ValidInput();
    input.dispatch_x = 1;
    input.user_data_6 = 100;
    if (MakeA912CopyPlan(input).byte_count != 64 * sizeof(u32)) return 2;
    input = ValidInput();
    input.user_data_6 = 65;
    if (MakeA912CopyPlan(input).byte_count != 65 * sizeof(u32)) return 3;
    input = ValidInput();
    input.dispatch_x = 0;
    if (MakeA912CopyPlan(input).kind != A912CopyPlanKind::NoOp) return 4;
    input = ValidInput();
    input.user_data_6 = 0;
    if (MakeA912CopyPlan(input).kind != A912CopyPlanKind::NoOp) return 5;
    input = ValidInput();
    input.destination_address = input.source_address;
    if (MakeA912CopyPlan(input).kind != A912CopyPlanKind::NoOp) return 6;

    // Identity, metadata, workgroup, dispatch, and side-resource guard failures fall back.
    const std::array<void (*)(A912CopyPlanInput&), 19> guard_failures{
        [](auto& v) { v.shader_hash ^= 1; },
        [](auto& v) { v.buffer_count = 3; },
        [](auto& v) { v.source_is_guest_read_only = false; },
        [](auto& v) { v.destination_is_guest_written = false; },
        [](auto& v) { v.buffers_are_copy_compatible = false; },
        [](auto& v) { v.descriptors_are_untransformed = false; },
        [](auto& v) { v.has_texture_aliases = true; },
        [](auto& v) { v.has_other_resources = true; },
        [](auto& v) { v.has_user_data_6 = false; },
        [](auto& v) { v.workgroup_size_x = 32; },
        [](auto& v) { v.workgroup_size_y = 2; },
        [](auto& v) { v.dispatch_y = 2; },
        [](auto& v) { v.dispatch_z = 2; },
        [](auto& v) { v.start_x = 1; },
        [](auto& v) { v.start_y = 1; },
        [](auto& v) { v.start_z = 1; },
        [](auto& v) { v.partial_x = 1; },
        [](auto& v) { v.partial_y = 1; },
        [](auto& v) { v.partial_z = 1; },
    };
    for (const auto fail : guard_failures) {
        input = ValidInput();
        fail(input);
        if (MakeA912CopyPlan(input).kind != A912CopyPlanKind::Fallback) return 7;
    }

    // Missing/invalid descriptors, unaligned element sizes, and address overflow fall back.
    input = ValidInput(); input.buffer_count = 1;
    if (MakeA912CopyPlan(input).kind != A912CopyPlanKind::Fallback) return 8;
    input = ValidInput(); input.source_size = 0;
    if (MakeA912CopyPlan(input).kind != A912CopyPlanKind::Fallback) return 9;
    input = ValidInput(); input.destination_size = 0;
    if (MakeA912CopyPlan(input).kind != A912CopyPlanKind::Fallback) return 10;
    input = ValidInput(); input.source_address = 0;
    if (MakeA912CopyPlan(input).kind != A912CopyPlanKind::Fallback) return 11;
    input = ValidInput(); input.source_address += 1;
    if (MakeA912CopyPlan(input).kind != A912CopyPlanKind::Fallback) return 22;
    input = ValidInput(); input.destination_address += 2;
    if (MakeA912CopyPlan(input).kind != A912CopyPlanKind::Fallback) return 23;
    input = ValidInput(); input.source_size = 510;
    if (MakeA912CopyPlan(input).kind != A912CopyPlanKind::Fallback) return 12;
    input = ValidInput(); input.destination_size = 510;
    if (MakeA912CopyPlan(input).kind != A912CopyPlanKind::Fallback) return 13;
    input = ValidInput(); input.user_data_6 = input.source_size / sizeof(u32) + 1;
    if (MakeA912CopyPlan(input).kind != A912CopyPlanKind::Fallback) return 14;
    input = ValidInput(); input.user_data_6 = input.destination_size / sizeof(u32) + 1;
    if (MakeA912CopyPlan(input).kind != A912CopyPlanKind::Fallback) return 15;
    input = ValidInput(); input.source_address = (u64{1} << 40) - 4; input.source_size = 8;
    if (MakeA912CopyPlan(input).kind != A912CopyPlanKind::Fallback) return 16;

    // Any partial descriptor overlap is rejected, including containment and one-byte shifts.
    input = ValidInput(); input.destination_address = input.source_address + 4;
    if (MakeA912CopyPlan(input).kind != A912CopyPlanKind::Fallback) return 17;
    input = ValidInput(); input.destination_address = input.source_address - 4;
    if (MakeA912CopyPlan(input).kind != A912CopyPlanKind::Fallback) return 18;
    input = ValidInput(); input.destination_address = input.source_address + 512;
    input.destination_size = 4;
    input.user_data_6 = 1;
    if (MakeA912CopyPlan(input).kind != A912CopyPlanKind::Copy) return 19;

    // The user-data bound prevents reads beyond either descriptor even with a large dispatch.
    input = ValidInput(); input.user_data_6 = 129;
    if (MakeA912CopyPlan(input).kind != A912CopyPlanKind::Fallback) return 20;

    // Physical Vulkan ranges must fit their buffers and never alias a source/destination byte.
    using VideoCore::CanCopyGpuBufferRanges;
    if (!CanCopyGpuBufferRanges(0, 512, 0, 512, 320, false)) return 24;
    if (CanCopyGpuBufferRanges(0, 512, 0, 512, 320, true)) return 25;
    if (CanCopyGpuBufferRanges(0, 512, 4, 512, 320, true)) return 26;
    if (CanCopyGpuBufferRanges(4, 512, 0, 512, 320, true)) return 27;
    if (!CanCopyGpuBufferRanges(0, 1024, 320, 1024, 320, true)) return 28;
    if (CanCopyGpuBufferRanges(513, 512, 0, 512, 4, false)) return 29;
    if (CanCopyGpuBufferRanges(0, 512, 500, 512, 16, false)) return 30;
    if (CanCopyGpuBufferRanges(0, 512, 0, 512, 0, false)) return 31;
    constexpr u64 large_offset = u64{1} << 33;
    if (!CanCopyGpuBufferRanges(large_offset, large_offset + 512, 0, 512, 320, true)) return 32;
    constexpr u64 max_offset = std::numeric_limits<u64>::max();
    if (CanCopyGpuBufferRanges(max_offset - 1, max_offset, 0, 512, 4, false)) return 33;
    return 0;
}
