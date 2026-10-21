#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "shader_recompiler/resource.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"

namespace Vulkan {

class ComputePipeline;
class Instance;
class Scheduler;

namespace ComputeCapture {

struct BoundBuffer {
    u32 binding{};
    u32 descriptor_write_index{};
    Shader::BufferType type{};
    Shader::IR::Type used_types{};
    bool written{};
    bool formatted{};
    bool guest_backed{};
    VAddr guest_begin{};
    u64 guest_size{};
    u64 guest_offset{};
    VideoCore::Buffer* owner{};
    vk::DescriptorBufferInfo descriptor{};
    std::vector<u8> constant_bytes;
};

struct DispatchShape {
    std::array<u32, 3> groups{};
    std::array<u32, 3> local_size{};
};

struct GpuTimestampSample {
    u64 start_ticks{};
    u64 end_ticks{};
    float period_ns{};
    double duration_ns{};
    bool valid{};
    std::string reason;
};

struct ReplayResult {
    bool attempted{};
    bool captured{};
    bool outputs_equal{};
    u64 original_gpu_ns{};
    u64 active_gpu_ns{};
    GpuTimestampSample original_timing;
    GpuTimestampSample active_timing;
    u64 differing_bytes{};
    std::filesystem::path directory;
    std::string reason;
};

[[nodiscard]] bool IsEnabled();
[[nodiscard]] bool IsTarget(u64 guest_hash);
[[nodiscard]] ReplayResult CaptureAndReplay(const Instance& instance, Scheduler& scheduler,
                                            const ComputePipeline& pipeline,
                                            const Shader::PushData& push_data,
                                            const Pipeline::DescriptorWrites& game_writes,
                                            const std::vector<BoundBuffer>& buffers,
                                            const ComputeCaptureModules& modules,
                                            DispatchShape shape);
void MarkGameDispatchComplete(const ReplayResult& result);

} // namespace ComputeCapture
} // namespace Vulkan
