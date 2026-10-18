// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <bit>
#include <cassert>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>
#include "video_core/renderer_vulkan/vk_command_recorder.h"
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
void assert_fail_impl() { std::abort(); }
namespace Common { std::string GetCurrentThreadName() { return "dynamic-state-test"; } }
namespace Common::Log { std::unordered_map<std::string_view, std::shared_ptr<spdlog::logger>> ALL_LOGGERS; }
namespace {
std::vector<float> widths;
unsigned state_calls{}, viewport_calls{}, scissor_calls{}, pipeline_calls{}, render_calls{};
VKAPI_ATTR void VKAPI_CALL Width(VkCommandBuffer, float v) { widths.push_back(v); }
VKAPI_ATTR void VKAPI_CALL Depth(VkCommandBuffer, VkBool32) { ++state_calls; }
VKAPI_ATTR void VKAPI_CALL Viewports(VkCommandBuffer, uint32_t, uint32_t, const VkViewport*) { ++viewport_calls; }
VKAPI_ATTR void VKAPI_CALL CountedViewports(VkCommandBuffer, uint32_t, const VkViewport*) { ++viewport_calls; }
VKAPI_ATTR void VKAPI_CALL Scissors(VkCommandBuffer, uint32_t, uint32_t, const VkRect2D*) { ++scissor_calls; }
VKAPI_ATTR void VKAPI_CALL Pipeline(VkCommandBuffer, VkPipelineBindPoint, VkPipeline) { ++pipeline_calls; }
VKAPI_ATTR void VKAPI_CALL Begin(VkCommandBuffer, const VkRenderingInfo*) { ++render_calls; }
VKAPI_ATTR void VKAPI_CALL End(VkCommandBuffer) { ++render_calls; }
}
int main() {
    auto& d = vk::detail::defaultDispatchLoaderDynamic;
    d.vkCmdSetLineWidth = Width; d.vkCmdSetDepthTestEnable = Depth;
    d.vkCmdSetViewport = Viewports; d.vkCmdSetViewportWithCount = CountedViewports;
    d.vkCmdSetScissor = Scissors; d.vkCmdBindPipeline = Pipeline;
    d.vkCmdBeginRendering = Begin; d.vkCmdEndRendering = End;
    const vk::CommandBuffer cb{reinterpret_cast<VkCommandBuffer>(1)};
    unsetenv("SHADPS4_DYNAMIC_STATE_REUSE");
    Vulkan::CommandList baseline;
    Vulkan::CommandRecorder disabled{cb, &baseline};
    disabled.setLineWidth(1); disabled.setLineWidth(1);
    assert(baseline.Size() == 2); baseline.Replay(cb);
    setenv("SHADPS4_DYNAMIC_STATE_REUSE", "1", 1);
    Vulkan::CommandList list;
    Vulkan::CommandRecorder r{cb, &list};
    r.setLineWidth(1); r.setLineWidth(1); r.setLineWidth(2); r.setLineWidth(1);
    assert(list.Size() == 3);
    r.setDepthTestEnable(true); r.setDepthTestEnable(true);
    assert(list.Size() == 4);
    std::array<vk::Viewport, 2> v{vk::Viewport{0,0,100,100,0,1}, vk::Viewport{1,1,90,90,0,1}};
    r.setViewport(0, v); r.setViewport(0, v);
    assert(list.Size() == 5);
    r.setViewport(1, vk::ArrayProxy<const vk::Viewport>{1, v.data()});
    r.setViewport(0, v);
    r.setViewportWithCount(v); r.setViewportWithCount(v); r.setViewport(0, v);
    assert(list.Size() == 9);
    r.setScissor(0, vk::Rect2D{{0,0},{100,100}});
    r.setScissor(0, vk::Rect2D{{0,0},{100,100}});
    assert(list.Size() == 10);
    v[0].width = 999; // The queued viewport owns the original array.
    list.Replay(cb);
    assert(viewport_calls == 5 && scissor_calls == 1 && state_calls == 1);
    r.setLineWidth(1); assert(list.Size() == 1);
    r.bindPipeline(vk::PipelineBindPoint::eGraphics, vk::Pipeline{reinterpret_cast<VkPipeline>(2)});
    r.setLineWidth(1); assert(list.Size() == 3);
    r.bindPipeline(vk::PipelineBindPoint::eGraphics, vk::Pipeline{reinterpret_cast<VkPipeline>(2)});
    r.setLineWidth(1); assert(list.Size() == 3);
    r.beginRendering(vk::RenderingInfo{}); r.setLineWidth(1);
    r.endRendering(); r.setLineWidth(1);
    assert(list.Size() == 7); list.Replay(cb);
    assert(pipeline_calls == 1 && render_calls == 2);
    Vulkan::CommandList other;
    Vulkan::CommandRecorder{cb, &other}.setLineWidth(1);
    assert(other.Size() == 1);
    const auto before = widths.size();
    Vulkan::CommandRecorder{cb}.setLineWidth(1); Vulkan::CommandRecorder{cb}.setLineWidth(1);
    assert(widths.size() == before + 2);

    Vulkan::DynamicStateReuse memo;
    using K = Vulkan::DynamicStateReuse::Key;
    std::array<unsigned, 5> faces{1,2,3,4,5};
    assert(memo.ShouldRecord(K::StencilOp, 0, 0, 1, faces.data(), sizeof(faces)));
    assert(!memo.ShouldRecord(K::StencilOp, 0, 0, 1, faces.data(), sizeof(faces)));
    faces[0] = 2;
    assert(memo.ShouldRecord(K::StencilOp, 0, 0, 1, faces.data(), sizeof(faces)));
    faces[0] = 1;
    assert(memo.ShouldRecord(K::StencilOp, 0, 0, 1, faces.data(), sizeof(faces)));
    for (size_t key = 0; key < static_cast<size_t>(K::Count); ++key) {
        auto k = static_cast<K>(key);
        memo.Reset(); unsigned value = 0;
        assert(memo.ShouldRecord(k, 0, 0, 1, &value, sizeof(value)));
        assert(!memo.ShouldRecord(k, 0, 0, 1, &value, sizeof(value)));
        ++value; assert(memo.ShouldRecord(k, 0, 0, 1, &value, sizeof(value)));
        memo.Reset(); assert(memo.ShouldRecord(k, 0, 0, 1, &value, sizeof(value)));
    }
    const unsigned minus_zero = 0x80000000, plus_zero = 0;
    assert(memo.ShouldRecord(K::LineWidth, 0, 0, 1, &plus_zero, 4));
    assert(memo.ShouldRecord(K::LineWidth, 0, 0, 1, &minus_zero, 4));
    std::array<unsigned char, 513> large{};
    assert(memo.ShouldRecord(K::Viewport, 0, 0, 1, large.data(), large.size()));
    assert(memo.ShouldRecord(K::Viewport, 0, 0, 1, large.data(), large.size()));
    return 0;
}
