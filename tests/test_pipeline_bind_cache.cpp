// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdint>
#include <type_traits>
#include <vector>

#include "video_core/renderer_vulkan/vk_command_recorder.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace {

struct BindCall {
    VkPipelineBindPoint bind_point{};
    VkPipeline pipeline{};
    bool operator==(const BindCall&) const = default;
};

std::vector<BindCall> bind_calls;

VKAPI_ATTR void VKAPI_CALL FakeBindPipeline(VkCommandBuffer, VkPipelineBindPoint bind_point,
                                             VkPipeline pipeline) {
    bind_calls.push_back({bind_point, pipeline});
}

template <typename Handle>
Handle FakeHandle(std::uintptr_t value) {
    if constexpr (std::is_pointer_v<Handle>) {
        return reinterpret_cast<Handle>(value);
    } else {
        return static_cast<Handle>(value);
    }
}

vk::Pipeline Pipeline(std::uintptr_t value) {
    return vk::Pipeline{FakeHandle<VkPipeline>(value)};
}

} // namespace

int main() {
    using vk::PipelineBindPoint;

    auto& dispatcher = vk::detail::defaultDispatchLoaderDynamic;
    dispatcher.vkCmdBindPipeline = FakeBindPipeline;

    const auto graphics = Pipeline(1);
    const auto other_graphics = Pipeline(2);
    const auto compute = Pipeline(3);
    const auto command_buffer = vk::CommandBuffer{FakeHandle<VkCommandBuffer>(1)};

    // Each list suppresses repeats independently, with separate graphics and compute state.
    Vulkan::CommandList first_list;
    Vulkan::CommandRecorder first{command_buffer, &first_list};
    first.bindPipeline(PipelineBindPoint::eGraphics, graphics);
    first.bindPipeline(PipelineBindPoint::eGraphics, graphics);
    first.bindPipeline(PipelineBindPoint::eCompute, graphics);
    first.bindPipeline(PipelineBindPoint::eCompute, graphics);
    first.bindPipeline(PipelineBindPoint::eGraphics, other_graphics);
    first.bindPipeline(PipelineBindPoint::eGraphics, graphics);
    first.bindPipeline(static_cast<PipelineBindPoint>(77), compute);
    first.bindPipeline(static_cast<PipelineBindPoint>(77), compute);
    first.bindPipeline(PipelineBindPoint::eGraphics, {});
    first.bindPipeline(PipelineBindPoint::eGraphics, {});
    first.bindPipeline(PipelineBindPoint::eGraphics, graphics);
    if (first_list.Size() != 9) return 1;

    Vulkan::CommandList second_list;
    Vulkan::CommandRecorder second{command_buffer, &second_list};
    second.bindPipeline(PipelineBindPoint::eGraphics, graphics);
    second.bindPipeline(PipelineBindPoint::eGraphics, graphics);
    if (second_list.Size() != 1) return 2;

    // Replay clears list-local state. A bind after replay must be recorded again.
    first_list.Replay(command_buffer);
    const std::vector<BindCall> expected{
        {VK_PIPELINE_BIND_POINT_GRAPHICS, static_cast<VkPipeline>(graphics)},
        {VK_PIPELINE_BIND_POINT_COMPUTE, static_cast<VkPipeline>(graphics)},
        {VK_PIPELINE_BIND_POINT_GRAPHICS, static_cast<VkPipeline>(other_graphics)},
        {VK_PIPELINE_BIND_POINT_GRAPHICS, static_cast<VkPipeline>(graphics)},
        {static_cast<VkPipelineBindPoint>(77), static_cast<VkPipeline>(compute)},
        {static_cast<VkPipelineBindPoint>(77), static_cast<VkPipeline>(compute)},
        {VK_PIPELINE_BIND_POINT_GRAPHICS, VkPipeline{}},
        {VK_PIPELINE_BIND_POINT_GRAPHICS, VkPipeline{}},
        {VK_PIPELINE_BIND_POINT_GRAPHICS, static_cast<VkPipeline>(graphics)},
    };
    if (bind_calls != expected || first_list.Size() != 0) return 3;
    first.bindPipeline(PipelineBindPoint::eGraphics, graphics);
    first.bindPipeline(PipelineBindPoint::eGraphics, graphics);
    if (first_list.Size() != 1) return 4;
    first_list.Replay(command_buffer);
    if (bind_calls.size() != 10) return 5;

    // A separate list starts unknown even if another list replayed the same pipeline.
    second_list.Replay(command_buffer);
    if (bind_calls.size() != 11) return 6;

    // Direct mode remains an unconditional pass-through.
    Vulkan::CommandRecorder direct{command_buffer};
    direct.bindPipeline(PipelineBindPoint::eGraphics, graphics);
    direct.bindPipeline(PipelineBindPoint::eGraphics, graphics);
    if (bind_calls.size() != 13) return 7;

    return 0;
}
