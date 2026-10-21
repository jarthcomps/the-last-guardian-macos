#include "video_core/renderer_vulkan/vk_compute_dispatch_capture.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <span>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <vk_mem_alloc.h>
#include <xxhash.h>

#include "common/logging/log.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/renderer_vulkan/vk_compute_pipeline.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/compute_dispatch_capture_plan.h"

namespace Vulkan::ComputeCapture {
namespace {

using Json = nlohmann::json;

constexpr u64 ReadbackChunkBytes = 16ULL * 1024 * 1024;
constexpr auto TargetHashes = CaptureTargetHashes;

std::atomic<u64> aggregate_capture_bytes{};
std::atomic_flag capture_in_flight = ATOMIC_FLAG_INIT;
std::array<std::atomic<u32>, TargetHashes.size()> capture_successes{};
std::array<std::atomic<u32>, TargetHashes.size()> capture_attempts{};

struct CaptureInFlightGuard {
    CaptureInFlightGuard() : acquired{!capture_in_flight.test_and_set(std::memory_order_acquire)} {}
    ~CaptureInFlightGuard() {
        if (acquired) {
            capture_in_flight.clear(std::memory_order_release);
        }
    }

    bool acquired{};
};

struct GroupData {
    u64 source_identity{};
    u64 source_offset{};
    u64 size{};
    VideoCore::Buffer* source{};
    std::vector<u8> input;
    std::vector<u8> original_output;
    std::vector<u8> active_output;
    std::unique_ptr<VideoCore::Buffer> replay_buffer;
    std::vector<u32> bindings;
    bool guest_backed{};
    bool written{};
};

struct ResourceMap {
    u32 group{std::numeric_limits<u32>::max()};
    u64 clone_offset{};
};

struct CapturePlan {
    AliasPlan aliases;
    std::vector<GroupData> groups;
    std::vector<ResourceMap> resources;
    u64 clone_bytes{};
    u64 output_bytes{};
};

struct CaptureStatusGuard {
    ReplayResult& result;

    ~CaptureStatusGuard() {
        if (result.directory.empty()) {
            return;
        }
        try {
            std::error_code error;
            std::filesystem::create_directories(result.directory, error);
            if (error) {
                return;
            }
            const Json status{
                {"schema_version", 1},
                {"attempted", result.attempted},
                {"captured", result.captured},
                {"outputs_equal", result.outputs_equal},
                {"original_gpu_ns", result.original_gpu_ns},
                {"active_gpu_ns", result.active_gpu_ns},
                {"timing_valid", result.original_timing.valid && result.active_timing.valid},
                {"original_timing", {{"start_ticks", result.original_timing.start_ticks},
                                     {"end_ticks", result.original_timing.end_ticks},
                                     {"period_ns", result.original_timing.period_ns},
                                     {"duration_ns", result.original_timing.duration_ns},
                                     {"valid", result.original_timing.valid},
                                     {"reason", result.original_timing.reason}}},
                {"active_timing", {{"start_ticks", result.active_timing.start_ticks},
                                   {"end_ticks", result.active_timing.end_ticks},
                                   {"period_ns", result.active_timing.period_ns},
                                   {"duration_ns", result.active_timing.duration_ns},
                                   {"valid", result.active_timing.valid},
                                   {"reason", result.active_timing.reason}}},
                {"differing_bytes", result.differing_bytes},
                {"reason", result.reason},
            };
            std::ofstream output(result.directory / "status.json",
                                 std::ios::out | std::ios::trunc);
            if (output) {
                output << status.dump(2) << '\n';
            }
        } catch (...) {
        }
    }
};

[[nodiscard]] std::string Hex(u64 value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

[[nodiscard]] u64 Xxh3(std::span<const u8> bytes) {
    return XXH3_64bits(bytes.data(), bytes.size());
}

[[nodiscard]] u64 Xxh3Spv(std::span<const u32> words) {
    return XXH3_64bits(words.data(), words.size_bytes());
}

[[nodiscard]] size_t TargetIndex(u64 guest_hash) {
    const auto it = std::find(TargetHashes.begin(), TargetHashes.end(), guest_hash);
    return it == TargetHashes.end() ? TargetHashes.size()
                                    : static_cast<size_t>(std::distance(TargetHashes.begin(), it));
}

[[nodiscard]] bool WriteBytes(const std::filesystem::path& path,
                              std::span<const u8> bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return file.good();
}

[[nodiscard]] bool WriteSpv(const std::filesystem::path& path,
                            std::span<const u32> words) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(words.data()),
               static_cast<std::streamsize>(words.size_bytes()));
    return file.good();
}

// The environment does not change while the emulator runs, so these variables are read once.
// IsEnabled runs on every draw and dispatch. An armed directory is still checked on disk per call.
[[nodiscard]] const std::filesystem::path& CaptureDirectoryFromEnvironment() {
    static const std::filesystem::path directory = [] {
        const char* value = std::getenv("SHADPS4_CAPTURE_COMPUTE_DIR");
        return value ? std::filesystem::path{value} : std::filesystem::path{};
    }();
    return directory;
}

[[nodiscard]] const std::string& CaptureHashesFromEnvironment() {
    static const std::string selected = [] {
        const char* value = std::getenv("SHADPS4_CAPTURE_COMPUTE_HASH");
        return value ? std::string{value} : std::string{};
    }();
    return selected;
}

[[nodiscard]] bool IsReadableCaptureDirectory(std::filesystem::path& root,
                                              std::string& reason) {
    const auto& directory = CaptureDirectoryFromEnvironment();
    if (directory.empty()) {
        reason = "SHADPS4_CAPTURE_COMPUTE_DIR is unset";
        return false;
    }
    root = directory;
    if (!IsCaptureArmed(root)) {
        reason = "capture has not been armed after world warmup";
        return false;
    }
    return true;
}

[[nodiscard]] bool CreateReplayModule(const vk::Device device, std::span<const u32> words,
                                      vk::UniqueShaderModule& module, std::string& reason) {
    if (words.empty() || words.size_bytes() % sizeof(u32) != 0) {
        reason = "empty or malformed SPIR-V payload";
        return false;
    }
    const vk::ShaderModuleCreateInfo create_info{
        .codeSize = words.size_bytes(),
        .pCode = words.data(),
    };
    auto [result, created] = device.createShaderModuleUnique(create_info);
    if (result != vk::Result::eSuccess) {
        reason = fmt::format("shader module creation failed: {}", vk::to_string(result));
        return false;
    }
    module = std::move(created);
    return true;
}

[[nodiscard]] bool CreateReplayPipeline(const vk::Device device, vk::PipelineLayout layout,
                                        vk::PipelineCache cache,
                                        vk::ShaderModule module, bool subgroup_size_64_supported,
                                        u32 required_subgroup_size,
                                        vk::UniquePipeline& pipeline, std::string& reason) {
    const vk::PipelineShaderStageRequiredSubgroupSizeCreateInfo subgroup_size{
        .requiredSubgroupSize = required_subgroup_size,
    };
    const vk::PipelineShaderStageCreateInfo shader_stage{
        .pNext = subgroup_size_64_supported ? &subgroup_size : nullptr,
        .stage = vk::ShaderStageFlagBits::eCompute,
        .module = module,
        .pName = "main",
    };
    const vk::ComputePipelineCreateInfo pipeline_info{
        .stage = shader_stage,
        .layout = layout,
    };
    auto [result, created] = device.createComputePipelineUnique(cache, pipeline_info);
    if (result != vk::Result::eSuccess) {
        reason = fmt::format("compute pipeline creation failed: {}", vk::to_string(result));
        return false;
    }
    pipeline = std::move(created);
    return true;
}

[[nodiscard]] bool BuildCapturePlan(const std::vector<BoundBuffer>& buffers, CapturePlan& plan,
                                    std::string& reason) {
    plan.resources.resize(buffers.size());
    std::vector<bool> flatbuf_resource(buffers.size());
    std::vector<DescriptorRange> ranges;
    std::vector<size_t> range_resources;
    for (size_t i = 0; i < buffers.size(); ++i) {
        const auto& buffer = buffers[i];
        if (buffer.type == Shader::BufferType::Flatbuf) {
            if (buffer.constant_bytes.empty() ||
                buffer.constant_bytes.size() != buffer.descriptor.range) {
                reason = fmt::format("Flatbuf binding {} has incomplete copied constants",
                                     buffer.binding);
                return false;
            }
            GroupData group{};
            group.size = buffer.constant_bytes.size();
            group.input = buffer.constant_bytes;
            group.bindings.push_back(buffer.binding);
            plan.resources[i] = {static_cast<u32>(plan.groups.size()), 0};
            flatbuf_resource[i] = true;
            plan.clone_bytes += group.size;
            plan.groups.push_back(std::move(group));
            continue;
        }
        if (buffer.type != Shader::BufferType::Guest) {
            reason = fmt::format("unsupported special buffer type {} at binding {}",
                                 static_cast<u32>(buffer.type), buffer.binding);
            return false;
        }
        if (!buffer.guest_backed || buffer.owner == nullptr || !buffer.descriptor.buffer) {
            if (buffer.used_types != Shader::IR::Type::Void) {
                reason = fmt::format("used Guest binding {} is unbound", buffer.binding);
                return false;
            }
            continue;
        }
        if (buffer.guest_size == 0 || buffer.descriptor.range == 0 ||
            buffer.descriptor.range > buffer.owner->SizeBytes() ||
            buffer.descriptor.offset > buffer.owner->SizeBytes() - buffer.descriptor.range ||
            buffer.descriptor.offset % 4 != 0 || buffer.descriptor.range % 4 != 0) {
            reason = fmt::format("Guest binding {} has an invalid or unaligned range",
                                 buffer.binding);
            return false;
        }
        const auto source_id = static_cast<u64>(reinterpret_cast<std::uintptr_t>(
            static_cast<VkBuffer>(buffer.descriptor.buffer)));
        ranges.push_back({.binding = buffer.binding,
                          .guest_begin = buffer.guest_begin,
                          .guest_size = buffer.guest_size,
                          .guest_offset = buffer.guest_offset,
                          .source_identity = source_id,
                          .source_offset = buffer.descriptor.offset,
                          .descriptor_range = buffer.descriptor.range,
                          .guest_backed = true,
                          .written = buffer.written});
        range_resources.push_back(i);
    }

    plan.aliases = BuildAliasPlan(ranges);
    if (!plan.aliases.supported) {
        reason = plan.aliases.rejection_reason;
        return false;
    }
    const size_t guest_group_count = plan.aliases.groups.size();
    std::vector<size_t> group_first_resource(guest_group_count, buffers.size());
    for (size_t range_index = 0; range_index < ranges.size(); ++range_index) {
        const size_t resource_index = range_resources[range_index];
        const auto group_index = plan.aliases.group_for_binding[range_index];
        plan.resources[resource_index] = {
            group_index, plan.aliases.clone_offset_for_binding[range_index]};
        if (group_first_resource[group_index] == buffers.size()) {
            group_first_resource[group_index] = resource_index;
        }
    }
    std::vector<GroupData> guest_groups;
    guest_groups.reserve(guest_group_count);
    for (size_t i = 0; i < guest_group_count; ++i) {
        const auto& alias = plan.aliases.groups[i];
        const size_t first_resource = group_first_resource[i];
        if (first_resource == buffers.size()) {
            reason = "alias plan produced an empty group";
            return false;
        }
        const auto& first = buffers[first_resource];
        GroupData group{};
        group.source_identity = alias.source_identity;
        group.source_offset = alias.source_offset;
        group.size = alias.size;
        group.source = first.owner;
        group.bindings = alias.bindings;
        group.written = alias.written;
        group.guest_backed = true;
        guest_groups.push_back(std::move(group));
    }
    plan.groups.insert(plan.groups.begin(), std::make_move_iterator(guest_groups.begin()),
                       std::make_move_iterator(guest_groups.end()));
    for (size_t i = 0; i < plan.resources.size(); ++i) {
        if (flatbuf_resource[i]) {
            plan.resources[i].group += static_cast<u32>(guest_group_count);
        }
    }

    plan.clone_bytes = plan.aliases.clone_bytes;
    plan.output_bytes = 0;
    for (const auto& group : plan.groups) {
        if (!group.guest_backed) {
            plan.clone_bytes += group.size;
        }
        if (group.written) {
            plan.output_bytes += group.size;
        }
    }
    if (plan.clone_bytes > MaxCloneBytes) {
        reason = "clone byte budget exceeded";
        return false;
    }
    return true;
}

[[nodiscard]] bool ReadGuestInputs(const Instance& instance, Scheduler& scheduler,
                                   std::vector<GroupData>& groups, std::string& reason) {
    u64 max_group_size = 0;
    for (const auto& group : groups) {
        if (group.guest_backed) {
            max_group_size = std::max(max_group_size, group.size);
        }
    }
    if (max_group_size == 0) {
        return true;
    }
    const u64 staging_size = std::min(ReadbackChunkBytes, max_group_size);
    VideoCore::Buffer staging{instance, scheduler, VideoCore::MemoryUsage::Download, 0,
                              vk::BufferUsageFlagBits::eTransferDst, staging_size};
    if (staging.mapped_data.size() < staging_size) {
        reason = "readback staging allocation is not mapped";
        return false;
    }

    for (auto& group : groups) {
        if (!group.guest_backed) {
            continue;
        }
        if (!group.source || group.source_offset > group.source->SizeBytes() ||
            group.size > group.source->SizeBytes() - group.source_offset ||
            group.size % 4 != 0 || group.source_offset % 4 != 0) {
            reason = "alias group is outside its source buffer or not transfer aligned";
            return false;
        }
        group.input.resize(group.size);
        const auto source_barrier = group.source->GetBarrier(
            vk::AccessFlagBits2::eTransferRead, vk::PipelineStageFlagBits2::eTransfer, 0);
        if (source_barrier) {
            scheduler.CommandBuffer().pipelineBarrier2(vk::DependencyInfo{
                .dependencyFlags = vk::DependencyFlagBits::eByRegion,
                .bufferMemoryBarrierCount = 1,
                .pBufferMemoryBarriers = &*source_barrier,
            });
        }

        for (u64 copied = 0; copied < group.size;) {
            const u64 chunk = std::min(staging_size, group.size - copied);
            const auto staging_barrier = staging.GetBarrier(
                vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eTransfer, 0);
            if (staging_barrier) {
                scheduler.CommandBuffer().pipelineBarrier2(vk::DependencyInfo{
                    .dependencyFlags = vk::DependencyFlagBits::eByRegion,
                    .bufferMemoryBarrierCount = 1,
                    .pBufferMemoryBarriers = &*staging_barrier,
                });
            }
            const vk::BufferCopy copy{
                .srcOffset = group.source_offset + copied,
                .dstOffset = 0,
                .size = chunk,
            };
            scheduler.CommandBuffer().copyBuffer(group.source->Handle(), staging.Handle(), copy);
            const auto host_barrier = staging.GetBarrier(vk::AccessFlagBits2::eHostRead,
                                                         vk::PipelineStageFlagBits2::eHost, 0);
            if (host_barrier) {
                scheduler.CommandBuffer().pipelineBarrier2(vk::DependencyInfo{
                    .dependencyFlags = vk::DependencyFlagBits::eByRegion,
                    .bufferMemoryBarrierCount = 1,
                    .pBufferMemoryBarriers = &*host_barrier,
                });
            }
            scheduler.Finish();
            const VkResult invalidate =
                vmaInvalidateAllocation(instance.GetAllocator(), staging.buffer.allocation, 0,
                                        chunk);
            if (invalidate != VK_SUCCESS) {
                reason = fmt::format("readback invalidation failed: {}",
                                     vk::to_string(vk::Result{invalidate}));
                return false;
            }
            std::memcpy(group.input.data() + copied, staging.mapped_data.data(), chunk);
            copied += chunk;
        }
    }
    return true;
}

[[nodiscard]] bool MakeReplayBuffers(const Instance& instance, Scheduler& scheduler,
                                     std::vector<GroupData>& groups, std::string& reason) {
    for (auto& group : groups) {
        if (group.size == 0 || group.input.size() != group.size) {
            reason = "capture group has no complete input bytes";
            return false;
        }
        group.replay_buffer = std::make_unique<VideoCore::Buffer>(
            instance, scheduler, VideoCore::MemoryUsage::Upload, 0, VideoCore::AllFlags,
            group.size);
        if (group.replay_buffer->mapped_data.size() < group.size) {
            reason = "replay clone is not host mapped";
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ResetReplayInputs(const Instance& instance, std::vector<GroupData>& groups,
                                    std::string& reason) {
    for (auto& group : groups) {
        auto& clone = *group.replay_buffer;
        std::memcpy(clone.mapped_data.data(), group.input.data(), group.size);
        if (!clone.is_coherent) {
            const VkResult flush = vmaFlushAllocation(instance.GetAllocator(),
                                                      clone.buffer.allocation, 0, group.size);
            if (flush != VK_SUCCESS) {
                reason = fmt::format("replay input flush failed: {}",
                                     vk::to_string(vk::Result{flush}));
                return false;
            }
        }
        clone.access_mask = vk::AccessFlagBits2::eHostWrite;
        clone.stage = vk::PipelineStageFlagBits2::eHost;
    }
    return true;
}

[[nodiscard]] bool InvalidateReplayOutputs(const Instance& instance,
                                           std::vector<GroupData>& groups,
                                           std::string& reason) {
    for (auto& group : groups) {
        if (!group.written || group.replay_buffer->is_coherent) {
            continue;
        }
        const VkResult invalidate = vmaInvalidateAllocation(
            instance.GetAllocator(), group.replay_buffer->buffer.allocation, 0, group.size);
        if (invalidate != VK_SUCCESS) {
            reason = fmt::format("replay output invalidation failed: {}",
                                 vk::to_string(vk::Result{invalidate}));
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool CreateReplayDescriptors(const std::vector<BoundBuffer>& buffers,
                                           const std::vector<ResourceMap>& resource_map,
                                           const std::vector<GroupData>& groups,
                                           const Pipeline::DescriptorWrites& game_writes,
                                           std::vector<vk::DescriptorBufferInfo>& infos,
                                           Pipeline::DescriptorWrites& writes,
                                           std::string& reason) {
    infos.resize(buffers.size());
    writes = game_writes;
    for (size_t i = 0; i < buffers.size(); ++i) {
        const auto& buffer = buffers[i];
        if (buffer.descriptor_write_index >= writes.size()) {
            reason = "descriptor write index is outside the active pipeline layout";
            return false;
        }
        const auto group_index = resource_map[i].group;
        if (group_index == std::numeric_limits<u32>::max()) {
            infos[i] = buffer.descriptor;
        } else {
            if (group_index >= groups.size() || !groups[group_index].replay_buffer) {
                reason = "descriptor references a missing replay clone";
                return false;
            }
            infos[i] = {
                .buffer = groups[group_index].replay_buffer->Handle(),
                .offset = resource_map[i].clone_offset,
                .range = buffer.descriptor.range,
            };
        }
        writes[buffer.descriptor_write_index].pBufferInfo = &infos[i];
    }
    return true;
}

void RecordCloneComputeBarriers(Scheduler& scheduler, std::vector<GroupData>& groups) {
    for (auto& group : groups) {
        const auto barrier = group.replay_buffer->GetBarrier(
            vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
            vk::PipelineStageFlagBits2::eComputeShader, 0);
        if (barrier) {
            scheduler.CommandBuffer().pipelineBarrier2(vk::DependencyInfo{
                .dependencyFlags = vk::DependencyFlagBits::eByRegion,
                .bufferMemoryBarrierCount = 1,
                .pBufferMemoryBarriers = &*barrier,
            });
        }
    }
}

void RecordCloneHostReadBarriers(Scheduler& scheduler, std::vector<GroupData>& groups) {
    std::vector<vk::BufferMemoryBarrier2> barriers;
    barriers.reserve(groups.size());
    for (const auto& group : groups) {
        if (!group.written) {
            continue;
        }
        barriers.push_back({
            .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eHost,
            .dstAccessMask = vk::AccessFlagBits2::eHostRead,
            .buffer = group.replay_buffer->Handle(),
            .offset = 0,
            .size = group.size,
        });
    }
    if (!barriers.empty()) {
        scheduler.CommandBuffer().pipelineBarrier2(vk::DependencyInfo{
            .dependencyFlags = vk::DependencyFlagBits::eByRegion,
            .bufferMemoryBarrierCount = static_cast<u32>(barriers.size()),
            .pBufferMemoryBarriers = barriers.data(),
        });
    }
}

void RecordSourceRestoreBarriers(Scheduler& scheduler,
                                 const std::vector<BoundBuffer>& buffers) {
    struct Restore {
        VideoCore::Buffer* buffer{};
        bool written{};
    };
    std::vector<Restore> restores;
    for (const auto& binding : buffers) {
        if (!binding.owner || !binding.guest_backed) {
            continue;
        }
        const auto it = std::find_if(restores.begin(), restores.end(), [&](const Restore& item) {
            return item.buffer == binding.owner;
        });
        if (it == restores.end()) {
            restores.push_back({binding.owner, binding.written});
        } else {
            it->written |= binding.written;
        }
    }
    std::vector<vk::BufferMemoryBarrier2> barriers;
    barriers.reserve(restores.size());
    for (const auto& restore : restores) {
        if (const auto barrier = restore.buffer->GetBarrier(
                restore.written ? vk::AccessFlagBits2::eShaderRead |
                                      vk::AccessFlagBits2::eShaderWrite
                                : vk::AccessFlagBits2::eShaderRead,
                vk::PipelineStageFlagBits2::eComputeShader, 0)) {
            barriers.push_back(*barrier);
        }
    }
    if (!barriers.empty()) {
        scheduler.CommandBuffer().pipelineBarrier2(vk::DependencyInfo{
            .dependencyFlags = vk::DependencyFlagBits::eByRegion,
            .bufferMemoryBarrierCount = static_cast<u32>(barriers.size()),
            .pBufferMemoryBarriers = barriers.data(),
        });
    }
}

[[nodiscard]] GpuTimestampSample ReadGpuTimes(const vk::Device device, vk::QueryPool pool,
                                              float period, u32 start_query, u32 end_query) {
    GpuTimestampSample sample{.period_ns = period};
    std::array<u64, 2> timestamps{};
    const std::array<u32, 2> query_indices{start_query, end_query};
    for (size_t i = 0; i < timestamps.size(); ++i) {
        const vk::Result result = device.getQueryPoolResults(
            pool, query_indices[i], 1, sizeof(u64), &timestamps[i], sizeof(u64),
            vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
        if (result != vk::Result::eSuccess) {
            sample.reason = fmt::format("timestamp query read failed: {}", vk::to_string(result));
            return sample;
        }
    }
    sample.start_ticks = timestamps[0];
    sample.end_ticks = timestamps[1];
    const auto interval = EvaluateTimestampInterval(timestamps[0], timestamps[1], period);
    if (interval.state != TimestampIntervalState::Valid) {
        sample.reason = TimestampIntervalReason(interval.state);
        return sample;
    }
    sample.valid = true;
    sample.duration_ns = interval.duration_ns;
    return sample;
}

[[nodiscard]] bool SaveCapture(const ReplayResult& result, const ComputeCaptureModules& modules,
                               const std::vector<BoundBuffer>& buffers,
                               const std::vector<ResourceMap>& resource_map,
                               const std::vector<GroupData>& groups,
                               const Shader::PushData& push_data, DispatchShape shape,
                               u64 aggregate_bytes, std::string& reason) {
    std::error_code error;
    std::filesystem::create_directories(result.directory, error);
    if (error) {
        reason = fmt::format("cannot create capture directory: {}", error.message());
        return false;
    }
    if (!WriteSpv(result.directory / "shader-original.spv", modules.original_spv) ||
        !WriteSpv(result.directory / "shader-active.spv", modules.active_spv)) {
        reason = "failed to write SPIR-V capture";
        return false;
    }
    const auto push_span = std::span<const u8>{reinterpret_cast<const u8*>(&push_data),
                                               sizeof(push_data)};
    if (!WriteBytes(result.directory / "push-data.bin", push_span)) {
        reason = "failed to write push data";
        return false;
    }

    Json manifest{
        {"schema_version", 1},
        {"capture_kind", "vulkan-storage-buffer-differential"},
        {"game_shader_hash", Hex(modules.guest_hash)},
        {"program_hash", Hex(modules.program_hash)},
        {"permutation", modules.permutation},
        {"replacement_id", modules.replacement_id},
        {"replacement_version", modules.replacement_version},
        {"replacement_cache_identity", Hex(modules.replacement_cache_identity)},
        {"shader", {{"original_file", "shader-original.spv"},
                    {"original_xxh3_64", Hex(Xxh3Spv(modules.original_spv))},
                    {"active_file", "shader-active.spv"},
                    {"active_xxh3_64", Hex(Xxh3Spv(modules.active_spv))}}},
        {"specialization", {{"stage", "compute"},
                            {"pipeline_specialization_constants", Json::array()},
                            {"required_subgroup_size", modules.required_subgroup_size},
                            {"subgroup_size_64_supported", modules.subgroup_size_64_supported},
                            {"profile_subgroup_size", modules.profile_subgroup_size},
                            {"permutation_baked_into_spirv", true}}},
        {"descriptor_layout", {{"set", 0}, {"descriptor_type", "storage_buffer"}}},
        {"dispatch", {{"groups", shape.groups}, {"local_size", shape.local_size}}},
        {"push_data", {{"file", "push-data.bin"},
                        {"bytes", sizeof(push_data)},
                        {"xxh3_64", Hex(Xxh3(push_span))}}},
        {"limits", {{"max_clone_bytes", MaxCloneBytes},
                     {"aggregate_bytes_after_capture", aggregate_bytes}}},
        {"groups", Json::array()},
        {"bindings", Json::array()},
        {"replay", {{"warmup_dispatches_per_variant", 1},
                     {"timing_is_diagnostic_only", true},
                     {"timing_samples_per_variant", 1},
                     {"timing_valid", result.original_timing.valid && result.active_timing.valid},
                     {"original_timing", {{"start_ticks", result.original_timing.start_ticks},
                                          {"end_ticks", result.original_timing.end_ticks},
                                          {"period_ns", result.original_timing.period_ns},
                                          {"duration_ns", result.original_timing.duration_ns},
                                          {"valid", result.original_timing.valid},
                                          {"reason", result.original_timing.reason}}},
                     {"active_timing", {{"start_ticks", result.active_timing.start_ticks},
                                        {"end_ticks", result.active_timing.end_ticks},
                                        {"period_ns", result.active_timing.period_ns},
                                        {"duration_ns", result.active_timing.duration_ns},
                                        {"valid", result.active_timing.valid},
                                        {"reason", result.active_timing.reason}}},
                     {"original_gpu_ns", result.original_gpu_ns},
                     {"active_gpu_ns", result.active_gpu_ns},
                     {"written_outputs_equal", result.outputs_equal},
                     {"differing_bytes", result.differing_bytes}}},
    };

    for (size_t i = 0; i < groups.size(); ++i) {
        const auto& group = groups[i];
        const std::string prefix = fmt::format("resource-{:03}", i);
        const std::string input_name = prefix + ".input.bin";
        if (!WriteBytes(result.directory / input_name, group.input)) {
            reason = fmt::format("failed to write {}", input_name);
            return false;
        }
        Json group_record{
            {"group", i},
            {"guest_backed", group.guest_backed},
            {"source_identity", Hex(group.source_identity)},
            {"source_offset", group.source_offset},
            {"bytes", group.size},
            {"written", group.written},
            {"bindings", group.bindings},
            {"input_file", input_name},
            {"input_xxh3_64", Hex(Xxh3(group.input))},
        };
        if (group.written) {
            const std::string original_name = prefix + ".original-output.bin";
            const std::string active_name = prefix + ".active-output.bin";
            if (!WriteBytes(result.directory / original_name, group.original_output) ||
                !WriteBytes(result.directory / active_name, group.active_output)) {
                reason = fmt::format("failed to write output blobs for group {}", i);
                return false;
            }
            group_record["original_output_file"] = original_name;
            group_record["original_output_xxh3_64"] = Hex(Xxh3(group.original_output));
            group_record["active_output_file"] = active_name;
            group_record["active_output_xxh3_64"] = Hex(Xxh3(group.active_output));
        }
        manifest["groups"].push_back(std::move(group_record));
    }

    for (size_t i = 0; i < buffers.size(); ++i) {
        const auto& buffer = buffers[i];
        const auto& mapping = resource_map[i];
        Json record{
            {"resource_index", i},
            {"set", 0},
            {"binding", buffer.binding},
            {"descriptor_write_index", buffer.descriptor_write_index},
            {"type", buffer.type == Shader::BufferType::Flatbuf ? "flatbuf" : "guest"},
            {"used_types", static_cast<u32>(buffer.used_types)},
            {"access", buffer.written ? "read_write" : "read_only"},
            {"formatted", buffer.formatted},
            {"descriptor_offset", buffer.descriptor.offset},
            {"descriptor_range", buffer.descriptor.range},
            {"clone_group", mapping.group == std::numeric_limits<u32>::max()
                                 ? Json(nullptr)
                                 : Json(mapping.group)},
            {"clone_offset", mapping.clone_offset},
        };
        if (buffer.guest_backed) {
            record["guest_begin"] = buffer.guest_begin;
            record["guest_descriptor_bytes"] = buffer.guest_size;
            record["guest_descriptor_offset"] = buffer.guest_offset;
        }
        manifest["bindings"].push_back(std::move(record));
    }

    manifest["capture_bytes"] = aggregate_bytes;
    std::ofstream output(result.directory / "manifest.json", std::ios::out | std::ios::trunc);
    if (!output) {
        reason = "failed to create capture manifest";
        return false;
    }
    output << manifest.dump(2) << '\n';
    if (!output.good()) {
        reason = "failed to write capture manifest";
        return false;
    }
    return true;
}

} // namespace

bool IsEnabled() {
    return IsCaptureEnabled(CaptureDirectoryFromEnvironment());
}

bool IsTarget(u64 guest_hash) {
    return CaptureTargetAllowed(TargetHashes, guest_hash, CaptureHashesFromEnvironment());
}

ReplayResult CaptureAndReplay(const Instance& instance, Scheduler& scheduler,
                              const ComputePipeline& pipeline,
                              const Shader::PushData& push_data,
                              const Pipeline::DescriptorWrites& game_writes,
                              const std::vector<BoundBuffer>& buffers,
                              const ComputeCaptureModules& modules, DispatchShape shape) {
    ReplayResult result{};
    result.attempted = true;
    std::filesystem::path root;
    if (!IsReadableCaptureDirectory(root, result.reason)) {
        return result;
    }
    CaptureInFlightGuard in_flight;
    if (!in_flight.acquired) {
        result.attempted = false;
        result.reason = "another compute capture is in flight";
        return result;
    }
    const size_t target_index = TargetIndex(modules.guest_hash);
    if (target_index >= TargetHashes.size()) {
        result.reason = "unknown capture target";
        return result;
    }
    static const u32 count_limit = CaptureCountLimit(std::getenv("SHADPS4_CAPTURE_COMPUTE_LIMIT"));
    const u32 captured = capture_successes[target_index].load(std::memory_order_relaxed);
    const u32 attempt = capture_attempts[target_index].fetch_add(1, std::memory_order_relaxed);
    if (captured >= count_limit || attempt >= count_limit * 3) {
        result.attempted = false;
        result.reason = "capture count limit reached";
        return result;
    }
    result.directory = root / fmt::format("{:08x}/attempt-{:03}",
                                         static_cast<u32>(modules.guest_hash), attempt + 1);
    std::error_code directory_error;
    if (std::filesystem::exists(result.directory, directory_error) || directory_error) {
        result.reason = "capture attempt directory already exists or cannot be inspected";
        result.directory.clear();
        return result;
    }
    CaptureStatusGuard status_guard{result};

    if (!IsTarget(modules.guest_hash) || modules.original_spv.empty() ||
        modules.active_spv.empty() || buffers.size() != game_writes.size()) {
        result.reason = "missing supported target metadata, shader words, or descriptor writes";
        return result;
    }
    const auto& info = pipeline.GetStage(Shader::SwStage::Compute);
    if (!info.images.empty() || !info.samplers.empty() || info.buffers.size() != buffers.size()) {
        result.reason = "capture v1 supports storage-buffer-only compute stages";
        return result;
    }
    for (u32 dimension : shape.local_size) {
        if (dimension == 0) {
            result.reason = "zero local workgroup dimension";
            return result;
        }
    }

    CapturePlan plan;
    if (!BuildCapturePlan(buffers, plan, result.reason)) {
        return result;
    }
    const u64 spv_bytes = modules.original_spv.size_bytes() + modules.active_spv.size_bytes();
    const u64 input_bytes = plan.clone_bytes + spv_bytes + sizeof(push_data);
    const u64 capture_bytes = input_bytes + plan.output_bytes * 2;
    const u64 aggregate = aggregate_capture_bytes.load(std::memory_order_relaxed);
    if (!WithinAggregateBudget(aggregate, capture_bytes)) {
        result.reason = "aggregate capture byte budget exceeded";
        return result;
    }

    const auto physical_properties = instance.GetPhysicalDevice().getProperties();
    if (physical_properties.limits.timestampPeriod <= 0.0f) {
        result.reason = "GPU timestamps are unavailable";
        return result;
    }
    const auto queue_properties = instance.GetPhysicalDevice().getQueueFamilyProperties();
    const u32 queue_index = instance.GetGraphicsQueueFamilyIndex();
    if (queue_index >= queue_properties.size() || queue_properties[queue_index].timestampValidBits == 0) {
        result.reason = "selected Vulkan queue does not support timestamps";
        return result;
    }

    const vk::Device device = instance.GetDevice();
    vk::UniqueShaderModule original_module;
    vk::UniqueShaderModule active_module;
    if (!CreateReplayModule(device, modules.original_spv, original_module, result.reason) ||
        !CreateReplayModule(device, modules.active_spv, active_module, result.reason)) {
        return result;
    }
    vk::UniquePipeline original_pipeline;
    vk::UniquePipeline active_pipeline;
    const auto pipeline_layout = pipeline.GetLayout();
    if (!CreateReplayPipeline(device, pipeline_layout, {}, original_module.get(),
                              modules.subgroup_size_64_supported,
                              modules.required_subgroup_size,
                              original_pipeline, result.reason) ||
        !CreateReplayPipeline(device, pipeline_layout, {}, active_module.get(),
                              modules.subgroup_size_64_supported,
                              modules.required_subgroup_size,
                              active_pipeline, result.reason)) {
        return result;
    }

    const vk::QueryPoolCreateInfo query_pool_info{
        .queryType = vk::QueryType::eTimestamp,
        .queryCount = 4,
    };
    auto [query_result, query_pool] = device.createQueryPoolUnique(query_pool_info);
    if (query_result != vk::Result::eSuccess) {
        result.reason = fmt::format("timestamp query pool creation failed: {}",
                                    vk::to_string(query_result));
        return result;
    }

    if (!ReadGuestInputs(instance, scheduler, plan.groups, result.reason)) {
        RecordSourceRestoreBarriers(scheduler, buffers);
        return result;
    }
    if (!MakeReplayBuffers(instance, scheduler, plan.groups, result.reason)) {
        RecordSourceRestoreBarriers(scheduler, buffers);
        return result;
    }
    if (!ResetReplayInputs(instance, plan.groups, result.reason)) {
        RecordSourceRestoreBarriers(scheduler, buffers);
        return result;
    }

    std::vector<vk::DescriptorBufferInfo> original_infos;
    std::vector<vk::DescriptorBufferInfo> active_infos;
    Pipeline::DescriptorWrites original_writes;
    Pipeline::DescriptorWrites active_writes;
    if (!CreateReplayDescriptors(buffers, plan.resources, plan.groups, game_writes,
                                 original_infos, original_writes, result.reason) ||
        !CreateReplayDescriptors(buffers, plan.resources, plan.groups, game_writes,
                                 active_infos, active_writes, result.reason)) {
        RecordSourceRestoreBarriers(scheduler, buffers);
        return result;
    }

    const Pipeline::BufferBarriers no_barriers;
    auto run_warmup = [&](vk::Pipeline replay_pipeline, Pipeline::DescriptorWrites& writes) {
        RecordCloneComputeBarriers(scheduler, plan.groups);
        pipeline.BindResources(writes, no_barriers, push_data, true);
        const auto command_buffer = scheduler.CommandBuffer();
        command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, replay_pipeline);
        command_buffer.dispatch(shape.groups[0], shape.groups[1], shape.groups[2]);
        scheduler.Finish();
        return ResetReplayInputs(instance, plan.groups, result.reason);
    };
    if (!run_warmup(original_pipeline.get(), original_writes) ||
        !run_warmup(active_pipeline.get(), active_writes)) {
        RecordSourceRestoreBarriers(scheduler, buffers);
        return result;
    }

    scheduler.CommandBuffer().resetQueryPool(query_pool.get(), 0, 4);
    auto run_timed = [&](vk::Pipeline replay_pipeline, Pipeline::DescriptorWrites& writes,
                         u32 query_start, u32 query_end, GpuTimestampSample& timing,
                         u64& duration,
                         std::vector<std::vector<u8>>& outputs) {
        RecordCloneComputeBarriers(scheduler, plan.groups);
        pipeline.BindResources(writes, no_barriers, push_data, true);
        const auto command_buffer = scheduler.CommandBuffer();
        command_buffer.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, query_pool.get(),
                                      query_start);
        command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, replay_pipeline);
        command_buffer.dispatch(shape.groups[0], shape.groups[1], shape.groups[2]);
        command_buffer.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, query_pool.get(),
                                      query_end);
        RecordCloneHostReadBarriers(scheduler, plan.groups);
        scheduler.Finish();
        timing = ReadGpuTimes(device, query_pool.get(), physical_properties.limits.timestampPeriod,
                              query_start, query_end);
        if (timing.valid) {
            duration = static_cast<u64>(timing.duration_ns);
        }
        if (!InvalidateReplayOutputs(instance, plan.groups, result.reason)) {
            return false;
        }
        outputs.resize(plan.groups.size());
        for (size_t i = 0; i < plan.groups.size(); ++i) {
            if (plan.groups[i].written) {
                const auto& clone = plan.groups[i].replay_buffer->mapped_data;
                outputs[i].assign(clone.begin(), clone.begin() + plan.groups[i].size);
            }
        }
        return true;
    };
    std::vector<std::vector<u8>> original_outputs;
    std::vector<std::vector<u8>> active_outputs;
    if (!run_timed(original_pipeline.get(), original_writes, 0, 1,
                   result.original_timing, result.original_gpu_ns, original_outputs) ||
        !ResetReplayInputs(instance, plan.groups, result.reason) ||
        !run_timed(active_pipeline.get(), active_writes, 2, 3,
                   result.active_timing, result.active_gpu_ns, active_outputs)) {
        scheduler.Finish();
        RecordSourceRestoreBarriers(scheduler, buffers);
        return result;
    }
    for (size_t i = 0; i < plan.groups.size(); ++i) {
        if (!plan.groups[i].written) {
            continue;
        }
        plan.groups[i].original_output = std::move(original_outputs[i]);
        plan.groups[i].active_output = std::move(active_outputs[i]);
        u64 differing{};
        const bool equal = OutputsEqual(plan.groups[i].original_output,
                                        plan.groups[i].active_output, differing);
        if (!equal) {
            result.outputs_equal = false;
        }
        result.differing_bytes += differing;
    }
    if (plan.output_bytes == 0) {
        result.reason = "capture contains no writable output ranges";
        RecordSourceRestoreBarriers(scheduler, buffers);
        return result;
    }

    result.outputs_equal = result.differing_bytes == 0;
    result.captured = true;
    if (!SaveCapture(result, modules, buffers, plan.resources, plan.groups, push_data, shape,
                     aggregate + capture_bytes, result.reason)) {
        result.captured = false;
    } else {
        aggregate_capture_bytes.fetch_add(capture_bytes, std::memory_order_relaxed);
        capture_successes[target_index].fetch_add(1, std::memory_order_relaxed);
    }
    RecordSourceRestoreBarriers(scheduler, buffers);
    return result;
}

void MarkGameDispatchComplete(const ReplayResult& result) {
    if (!result.attempted || result.directory.empty()) {
        return;
    }
    std::ofstream marker(result.directory / "game-dispatch-complete",
                         std::ios::out | std::ios::trunc);
    if (marker) {
        marker << "complete\n";
    }
}

} // namespace Vulkan::ComputeCapture
