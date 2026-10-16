// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <cstdlib>
#include "shader_recompiler/info.h"
#include "shader_recompiler/ir/type.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_hle.h"
#include "video_core/renderer_vulkan/copy_shader_hle_plan.h"

extern std::unique_ptr<AmdGpu::Liverpool> liverpool;

namespace Vulkan {

static constexpr u64 COPY_SHADER_HASH = 0xfefebf9f;
static constexpr u64 FLOAT_COPY_SHADER_HASH = 0xa912f7d4;

static_assert(static_cast<u32>(Shader::BufferType::Guest) == A912GuestBufferType);
static_assert(static_cast<u32>(Shader::IR::Type::F32) == A912F32Type);
static_assert(static_cast<u32>(Shader::IR::Type::U32 | Shader::IR::Type::F32 |
                               Shader::IR::Type::U16 | Shader::IR::Type::U8) ==
              A912FormattedTypes);
static_assert(static_cast<u32>(AmdGpu::DataFormat::Format32) == A912Format32);
static_assert(static_cast<u32>(AmdGpu::NumberFormat::Uint) == A912NumberFormatUint);
static_assert(static_cast<u32>(AmdGpu::NumberConversion::None) == A912NumberConversionNone);
static_assert(static_cast<u32>(AmdGpu::CompSwizzle::Zero) == A912SwizzleZero);
static_assert(static_cast<u32>(AmdGpu::CompSwizzle::Red) == A912SwizzleRed);
static_assert(VideoCore::BufferCache::CACHING_PAGESIZE == A912CopyStreamThreshold);

template <typename T>
static bool CanFetchDescriptor(const Shader::SharpFetch<T>& fetch, const Shader::Info& info) {
    for (u32 i = 0; i < Shader::SharpFetch<T>::N; ++i) {
        if (fetch.offsets[i] == Shader::UNKNOWN_LOCATION) {
            return false;
        }
        if (((fetch.load_mask >> i) & 1) != 0 && fetch.offsets[i] >= info.flattened_ud_buf.size()) {
            return false;
        }
    }
    return true;
}

static bool ShouldLogA912Diagnostic() {
    static const bool enabled =
        A912DiagnosticsEnabled(std::getenv("SHADPS4_A912_DIAGNOSTICS"));
    if (!enabled) {
        return false;
    }
    static std::atomic<u32> reports{};
    const u32 report_index = reports.fetch_add(1, std::memory_order_relaxed);
    return A912DiagnosticReportAllowed(enabled, report_index);
}

static bool ShouldLogA912CopySuccess() {
    static const bool enabled =
        A912DiagnosticsEnabled(std::getenv("SHADPS4_A912_DIAGNOSTICS"));
    if (!enabled) {
        return false;
    }
    static std::atomic<u32> reports{};
    const u32 report_index = reports.fetch_add(1, std::memory_order_relaxed);
    return A912DiagnosticReportAllowed(enabled, report_index);
}

static void LogA912Diagnostic(const Shader::Info& info,
                              const AmdGpu::ComputeProgram& cs_program,
                              Rasterizer& rasterizer) {
    if (!ShouldLogA912Diagnostic()) {
        return;
    }

    const bool has_user_data_6 = info.user_data.size() > 6;
    LOG_WARNING(Render_Vulkan,
                "A912 diagnostic hash={:#x} dispatch=({}, {}, {}) start=({}, {}, {}) "
                "wg_full=({}, {}, {}) "
                "wg_partial=({}, {}, {}) buffers={} images={} samplers={} fmasks={} "
                "storage_images={} dma={} shared={} scratch={} user_data_size={} ud6={} ",
                info.pgm_hash, cs_program.dim_x, cs_program.dim_y, cs_program.dim_z,
                cs_program.start_x, cs_program.start_y, cs_program.start_z,
                cs_program.num_thread_x.full, cs_program.num_thread_y.full,
                cs_program.num_thread_z.full, cs_program.num_thread_x.partial,
                cs_program.num_thread_y.partial, cs_program.num_thread_z.partial,
                info.buffers.size(), info.images.size(), info.samplers.size(), info.fmasks.size(),
                info.has_storage_images, info.uses_dma, cs_program.SharedMemSize(),
                info.shared_memory_scratch_size, info.user_data.size(),
                has_user_data_6 ? info.user_data[6] : 0);

    for (u32 i = 0; i < info.buffers.size(); ++i) {
        const auto& resource = info.buffers[i];
        LOG_WARNING(Render_Vulkan,
                    "A912 diagnostic buffer[{}] type={} used_types={:#x} read_only={} written={} "
                    "formatted={} post_op={} post_op_dw1_mask={:#x}",
                    i, static_cast<u32>(resource.buffer_type),
                    static_cast<u32>(resource.used_types), !resource.is_written,
                    resource.is_written, resource.is_formatted,
                    static_cast<u32>(resource.post_op),
                    resource.post_op_dw1_mask);
    }

    if (info.buffers.size() < 2 || !has_user_data_6 ||
        !CanFetchDescriptor(info.buffers[0].sharp_fetch, info) ||
        !CanFetchDescriptor(info.buffers[1].sharp_fetch, info)) {
        LOG_WARNING(Render_Vulkan,
                    "A912 diagnostic descriptor details unavailable: buffers={} userdata6={} "
                    "src_fetchable={} dst_fetchable={}",
                    info.buffers.size(), has_user_data_6,
                    info.buffers.size() > 0 && CanFetchDescriptor(info.buffers[0].sharp_fetch, info),
                    info.buffers.size() > 1 && CanFetchDescriptor(info.buffers[1].sharp_fetch, info));
        return;
    }

    const auto source = info.buffers[0].GetSharp(info);
    const auto destination = info.buffers[1].GetSharp(info);
    const bool source_valid = source.Valid() && bool(source);
    const bool destination_valid = destination.Valid() && bool(destination);
    const u32 source_size = source.GetSize();
    const u32 destination_size = destination.GetSize();
    const bool ranges_overlap = source_valid && destination_valid &&
                                CopyByteRangesOverlap(source.base_address, source_size,
                                                      destination.base_address, destination_size);
    auto& texture_cache = rasterizer.GetTextureCache();
    const bool source_alias = source_valid &&
                              (texture_cache.IsMeta(source.base_address).has_value() ||
                               bool(texture_cache.FindImageFromRange(source.base_address,
                                                                     source_size, false)));
    const bool destination_alias = destination_valid &&
                                   (texture_cache.IsMeta(destination.base_address).has_value() ||
                                    bool(texture_cache.FindImageFromRange(destination.base_address,
                                                                          destination_size,
                                                                          false)));
    LOG_WARNING(Render_Vulkan,
                "A912 diagnostic descriptors valid=({}, {}) source=(base={:#x} size={} stride={} "
                "data_format={} num_format={} dst_select=({},{},{},{}) num_conversion={} "
                "swizzle_enable={} add_tid_enable={} cache_swizzle={}) destination=(base={:#x} "
                "size={} stride={} data_format={} num_format={} dst_select=({},{},{},{}) "
                "num_conversion={} swizzle_enable={} add_tid_enable={} cache_swizzle={}) "
                "ud6={} ranges_overlap={} aliases=({}, {})",
                source_valid, destination_valid, source.base_address, source_size,
                source.GetStride(), source.data_format, source.num_format,
                source.dst_sel_x, source.dst_sel_y, source.dst_sel_z, source.dst_sel_w,
                static_cast<u32>(source.GetNumberConversion()), source.swizzle_enable,
                source.add_tid_enable, source.cache_swizzle, destination.base_address,
                destination_size, destination.GetStride(), destination.data_format,
                destination.num_format, destination.dst_sel_x, destination.dst_sel_y,
                destination.dst_sel_z, destination.dst_sel_w,
                static_cast<u32>(destination.GetNumberConversion()), destination.swizzle_enable,
                destination.add_tid_enable, destination.cache_swizzle, info.user_data[6],
                ranges_overlap,
                source_alias, destination_alias);
}

static A912CopyDescriptorFormat GetA912CopyDescriptorFormat(const AmdGpu::Buffer& buffer) {
    const auto swizzle = buffer.DstSelect();
    return {
        .data_format = static_cast<u32>(buffer.GetDataFmt()),
        .number_format = static_cast<u32>(buffer.GetNumberFmt()),
        .stride = buffer.GetStride(),
        .dst_select = {static_cast<u32>(swizzle.r), static_cast<u32>(swizzle.g),
                       static_cast<u32>(swizzle.b), static_cast<u32>(swizzle.a)},
        .number_conversion = static_cast<u32>(buffer.GetNumberConversion()),
        .swizzle_enable = buffer.swizzle_enable,
        .add_tid_enable = buffer.add_tid_enable,
        .cache_swizzle = buffer.cache_swizzle,
    };
}

static bool ExecuteCopyShaderHLE(const Shader::Info& info, const AmdGpu::ComputeProgram& cs_program,
                                 Rasterizer& rasterizer) {
    auto& scheduler = rasterizer.GetScheduler();
    auto& buffer_cache = rasterizer.GetBufferCache();

    // Copy shader defines three formatted buffers as inputs: control, source, and destination.
    const auto ctl_buf_sharp = info.buffers[0].GetSharp(info);
    const auto src_buf_sharp = info.buffers[1].GetSharp(info);
    const auto dst_buf_sharp = info.buffers[2].GetSharp(info);
    const auto buf_stride = src_buf_sharp.GetStride();
    ASSERT(buf_stride == dst_buf_sharp.GetStride());

    struct CopyShaderControl {
        u32 dst_idx;
        u32 src_idx;
        u32 end;
    };
    static_assert(sizeof(CopyShaderControl) == 12);
    ASSERT(ctl_buf_sharp.GetStride() == sizeof(CopyShaderControl));
    const auto ctl_buf = reinterpret_cast<const CopyShaderControl*>(ctl_buf_sharp.base_address);

    static std::vector<vk::BufferCopy> copies;
    copies.clear();
    copies.reserve(cs_program.dim_x);

    for (u32 i = 0; i < cs_program.dim_x; i++) {
        const auto& [dst_idx, src_idx, end] = ctl_buf[i];
        const u32 local_dst_offset = dst_idx * buf_stride;
        const u32 local_src_offset = src_idx * buf_stride;
        const u32 local_size = (end + 1) * buf_stride;
        copies.emplace_back(local_src_offset, local_dst_offset, local_size);
    }

    scheduler.EndRendering();

    static constexpr vk::MemoryBarrier READ_BARRIER{
        .srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
        .dstAccessMask = vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite,
    };
    static constexpr vk::MemoryBarrier WRITE_BARRIER{
        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
        .dstAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite,
    };
    scheduler.CommandBuffer().pipelineBarrier(
        vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTransfer,
        vk::DependencyFlagBits::eByRegion, READ_BARRIER, {}, {});

    static constexpr vk::DeviceSize MaxDistanceForMerge = 64_MB;
    u32 batch_start = 0;
    u32 batch_end = 0;

    while (batch_end < copies.size()) {
        // Place first copy into the current batch
        const auto& copy = copies[batch_start];
        auto src_offset_min = copy.srcOffset;
        auto src_offset_max = copy.srcOffset + copy.size;
        auto dst_offset_min = copy.dstOffset;
        auto dst_offset_max = copy.dstOffset + copy.size;

        for (++batch_end; batch_end < copies.size(); batch_end++) {
            // Compute new src and dst bounds if we were to batch this copy
            const auto& [src_offset, dst_offset, size] = copies[batch_end];
            auto new_src_offset_min = std::min(src_offset_min, src_offset);
            auto new_src_offset_max = std::max(src_offset_max, src_offset + size);
            if (new_src_offset_max - new_src_offset_min > MaxDistanceForMerge) {
                break;
            }

            auto new_dst_offset_min = std::min(dst_offset_min, dst_offset);
            auto new_dst_offset_max = std::max(dst_offset_max, dst_offset + size);
            if (new_dst_offset_max - new_dst_offset_min > MaxDistanceForMerge) {
                break;
            }

            // We can batch this copy
            src_offset_min = new_src_offset_min;
            src_offset_max = new_src_offset_max;
            dst_offset_min = new_dst_offset_min;
            dst_offset_max = new_dst_offset_max;
        }

        // Obtain buffers for the total source and destination ranges.
        const auto [src_buf, src_buf_offset] = buffer_cache.ObtainBuffer(
            src_buf_sharp.base_address + src_offset_min, src_offset_max - src_offset_min, false);
        const auto [dst_buf, dst_buf_offset] = buffer_cache.ObtainBuffer(
            dst_buf_sharp.base_address + dst_offset_min, dst_offset_max - dst_offset_min, true);

        // Apply found buffer base.
        const auto vk_copies = std::span{copies}.subspan(batch_start, batch_end - batch_start);
        for (auto& copy : vk_copies) {
            copy.srcOffset = copy.srcOffset - src_offset_min + src_buf_offset;
            copy.dstOffset = copy.dstOffset - dst_offset_min + dst_buf_offset;
        }

        // Execute buffer copies.
        LOG_TRACE(Render_Vulkan, "HLE buffer copy: src_size = {}, dst_size = {}",
                  src_offset_max - src_offset_min, dst_offset_max - dst_offset_min);
        scheduler.CommandBuffer().copyBuffer(src_buf->Handle(), dst_buf->Handle(), vk_copies);
        batch_start = batch_end;
    }

    scheduler.CommandBuffer().pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands,
        vk::DependencyFlagBits::eByRegion, WRITE_BARRIER, {}, {});

    return true;
}

static bool ExecuteFloatCopyShaderHLE(const Shader::Info& info,
                                     const AmdGpu::ComputeProgram& cs_program,
                                     Rasterizer& rasterizer) {
    LogA912Diagnostic(info, cs_program, rasterizer);
    if (info.buffers.size() != 2 || !info.images.empty() || !info.samplers.empty() ||
        !info.fmasks.empty() || info.has_storage_images || info.uses_dma ||
        cs_program.SharedMemSize() != 0 || info.shared_memory_scratch_size != 0 ||
        info.user_data.size() <= 6) {
        return false;
    }

    const auto& source_resource = info.buffers[0];
    const auto& destination_resource = info.buffers[1];
    if (!CanFetchDescriptor(source_resource.sharp_fetch, info) ||
        !CanFetchDescriptor(destination_resource.sharp_fetch, info)) {
        return false;
    }

    const auto source = source_resource.GetSharp(info);
    const auto destination = destination_resource.GetSharp(info);
    if (!source.Valid() || !destination.Valid() || !source || !destination) {
        return false;
    }
    const bool formatted_descriptors_are_compatible =
        AreA912ScalarUintFormatsMatching(GetA912CopyDescriptorFormat(source),
                                         GetA912CopyDescriptorFormat(destination));
    const bool resources_are_compatible = A912CopyResourcesCompatible({
        .source_buffer_type = static_cast<u32>(source_resource.buffer_type),
        .destination_buffer_type = static_cast<u32>(destination_resource.buffer_type),
        .source_used_types = static_cast<u32>(source_resource.used_types),
        .destination_used_types = static_cast<u32>(destination_resource.used_types),
        .source_is_written = source_resource.is_written,
        .destination_is_written = destination_resource.is_written,
        .source_is_formatted = source_resource.is_formatted,
        .destination_is_formatted = destination_resource.is_formatted,
        .source_post_op_is_none = source_resource.post_op == Shader::SharpFetchPostOp::None,
        .destination_post_op_is_none =
            destination_resource.post_op == Shader::SharpFetchPostOp::None,
        .formatted_descriptors_are_compatible = formatted_descriptors_are_compatible,
    });
    if (!resources_are_compatible) {
        return false;
    }
    auto& texture_cache = rasterizer.GetTextureCache();
    const auto source_image = texture_cache.FindImageFromRange(source.base_address,
                                                              source.GetSize(), false);
    const A912CopyAliases aliases{
        .source_metadata = texture_cache.IsMeta(source.base_address).has_value(),
        .destination_metadata = texture_cache.IsMeta(destination.base_address).has_value(),
        .source_image = bool(source_image),
        .destination_image = bool(texture_cache.FindImageFromRange(destination.base_address,
                                                                 destination.GetSize(), false)),
        .source_is_formatted = source_resource.is_formatted,
        .source_image_size_matches = source_image &&
                                    texture_cache.GetImage(source_image).info.guest_size ==
                                        source.GetSize(),
        .source_size = source.GetSize(),
    };
    static const bool image_source_enabled =
        A912ImageSourceCopyEnabled(std::getenv("SHADPS4_A912_IMAGE_SOURCE_COPY"));
    const bool aliases_are_compatible = A912CopyAliasesCompatible(aliases, image_source_enabled);
    const A912CopyPlan plan = MakeA912CopyPlan({
        .shader_hash = info.pgm_hash,
        .dispatch_x = cs_program.dim_x,
        .dispatch_y = cs_program.dim_y,
        .dispatch_z = cs_program.dim_z,
        .start_x = cs_program.start_x,
        .start_y = cs_program.start_y,
        .start_z = cs_program.start_z,
        .user_data_6 = info.user_data[6],
        .workgroup_size_x = cs_program.num_thread_x.full,
        .workgroup_size_y = cs_program.num_thread_y.full,
        .workgroup_size_z = cs_program.num_thread_z.full,
        .partial_x = cs_program.num_thread_x.partial,
        .partial_y = cs_program.num_thread_y.partial,
        .partial_z = cs_program.num_thread_z.partial,
        .source_size = source.GetSize(),
        .destination_size = destination.GetSize(),
        .source_address = source.base_address,
        .destination_address = destination.base_address,
        .buffer_count = static_cast<u32>(info.buffers.size()),
        .source_is_guest_read_only = !source_resource.IsSpecial() &&
                                     !source_resource.is_written,
        .destination_is_guest_written = !destination_resource.IsSpecial() &&
                                       destination_resource.is_written,
        .buffers_are_copy_compatible = true,
        .descriptors_are_untransformed =
            source_resource.post_op == Shader::SharpFetchPostOp::None &&
            destination_resource.post_op == Shader::SharpFetchPostOp::None &&
            !source.swizzle_enable && !destination.swizzle_enable && !source.add_tid_enable &&
            !destination.add_tid_enable,
        .has_texture_aliases = !aliases_are_compatible,
        .has_other_resources = !info.images.empty() || !info.samplers.empty() ||
                               !info.fmasks.empty() || info.has_storage_images || info.uses_dma ||
                               cs_program.SharedMemSize() != 0 ||
                               info.shared_memory_scratch_size != 0,
        .has_user_data_6 = info.user_data.size() > 6,
    });
    if (plan.kind == A912CopyPlanKind::Fallback) {
        return false;
    }
    if (plan.kind == A912CopyPlanKind::NoOp) {
        return true;
    }

    if (!rasterizer.GetBufferCache().CopyBufferGpu(plan.destination_address,
                                                   plan.source_address, plan.byte_count,
                                                   aliases.source_image ? source.GetSize() : 0)) {
        return false;
    }
    if (ShouldLogA912CopySuccess()) {
        LOG_WARNING(Render_Vulkan,
                    "A912 HLE copy succeeded hash={:#x} bytes={} source={:#x} destination={:#x} "
                    "image_source_descriptor_size={}",
                    info.pgm_hash, plan.byte_count, plan.source_address,
                    plan.destination_address, aliases.source_image ? source.GetSize() : 0);
    }
    return true;
}

bool ExecuteShaderHLE(const Shader::Info& info, const AmdGpu::Regs& regs,
                      const AmdGpu::ComputeProgram& cs_program, Rasterizer& rasterizer) {
    switch (info.pgm_hash) {
    case COPY_SHADER_HASH:
        return ExecuteCopyShaderHLE(info, cs_program, rasterizer);
    case FLOAT_COPY_SHADER_HASH:
        if (static const bool enabled = A912CopyShaderHleEnabled(
                std::getenv("SHADPS4_ENABLE_A912_COPY_HLE"),
                std::getenv("SHADPS4_DISABLE_A912_COPY_HLE"));
            !enabled) {
            return false;
        }
        return ExecuteFloatCopyShaderHLE(info, cs_program, rasterizer);
    default:
        return false;
    }
}

} // namespace Vulkan
