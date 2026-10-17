// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include "video_core/renderer_vulkan/vk_command_recorder.h"
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
void assert_fail_impl() { std::abort(); }
namespace Common { std::string GetCurrentThreadName() { return "constant-recorder-test"; } }
namespace Common::Log { std::unordered_map<std::string_view, std::shared_ptr<spdlog::logger>> ALL_LOGGERS; }
namespace {
struct Call { VkPipelineLayout layout; VkShaderStageFlags stages; unsigned offset; std::vector<unsigned char> bytes; };
std::vector<Call> calls;
VKAPI_ATTR void VKAPI_CALL Push(VkCommandBuffer, VkPipelineLayout layout,
    VkShaderStageFlags stages, uint32_t offset, uint32_t size, const void* data) {
    const auto* p = static_cast<const unsigned char*>(data);
    calls.push_back({layout, stages, offset, {p, p + size}});
}
template <typename T> T Handle(std::uintptr_t value) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(value);
    else return static_cast<T>(value);
}
}
#define CHECK(x) do { if (!(x)) return __LINE__; } while (false)
int main() {
    vk::detail::defaultDispatchLoaderDynamic.vkCmdPushConstants = Push;
    const vk::CommandBuffer cb{Handle<VkCommandBuffer>(1)};
    const vk::PipelineLayout layout{Handle<VkPipelineLayout>(2)};
    const auto stage = vk::ShaderStageFlagBits::eVertex;
    std::array<uint32_t, 4> data{1,2,3,4};
    unsetenv("SHADPS4_PUSH_CONSTANT_REUSE");
    Vulkan::CommandList disabled;
    Vulkan::CommandRecorder baseline{cb, &disabled};
    baseline.pushConstants(layout, stage, 0, 16, data.data());
    baseline.pushConstants(layout, stage, 0, 16, data.data());
    CHECK(disabled.Size() == 2);
    disabled.Replay(cb);
    CHECK(calls.size() == 2);
    calls.clear();
    setenv("SHADPS4_PUSH_CONSTANT_REUSE", "1", 1);
    unsetenv("SHADPS4_CONSTANT_REUSE_DIAGNOSTICS");
    Vulkan::CommandList list;
    Vulkan::CommandRecorder recorder{cb, &list};
    recorder.pushConstants(layout, stage, 0, 16, data.data());
    recorder.pushConstants(layout, stage, 0, 16, data.data());
    CHECK(list.Size() == 1);
    data[0] = 7;
    recorder.pushConstants(layout, stage, 0, 16, data.data());
    CHECK(list.Size() == 2);
    data[0] = 99; // Queued commands and remembered bytes own their contents.
    list.Replay(cb);
    CHECK(calls.size() == 2 && calls[0].bytes[0] == 1 && calls[1].bytes[0] == 7);
    calls.clear();
    data[0] = 7;
    recorder.pushConstants(layout, stage, 0, 16, data.data());
    CHECK(list.Size() == 1); // Replay reset.
    recorder.pushConstants(layout, vk::ShaderStageFlagBits::eCompute, 0, 16, data.data());
    recorder.pushConstants(layout, stage, 0, 16, data.data());
    recorder.pushConstants(layout, stage, 4, 4, data.data());
    recorder.pushConstants(layout, stage, 0, 16, data.data());
    recorder.pushConstants(vk::PipelineLayout{Handle<VkPipelineLayout>(3)}, stage, 0, 16, data.data());
    CHECK(list.Size() == 6);
    list.Replay(cb);
    CHECK(calls.size() == 6 && calls[3].offset == 4 && calls[3].bytes.size() == 4);
    Vulkan::CommandList second;
    Vulkan::CommandRecorder another{cb, &second};
    another.pushConstants(layout, stage, 0, 16, data.data());
    CHECK(second.Size() == 1);
    calls.clear();
    Vulkan::CommandRecorder direct{cb};
    direct.pushConstants(layout, stage, 0, 16, data.data());
    direct.pushConstants(layout, stage, 0, 16, data.data());
    CHECK(calls.size() == 2);
    return 0;
}
