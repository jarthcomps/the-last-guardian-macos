// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>
#include "video_core/renderer_vulkan/compute_dispatch_labels.h"
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
void assert_fail_impl() { std::abort(); }
namespace Common { std::string GetCurrentThreadName() { return "compute-label-test"; } }
namespace Common::Log { std::unordered_map<std::string_view, std::shared_ptr<spdlog::logger>> ALL_LOGGERS; }
namespace {
std::vector<std::string> events;
VKAPI_ATTR void VKAPI_CALL Begin(VkCommandBuffer, const VkDebugUtilsLabelEXT* l) { events.push_back(l->pLabelName); }
VKAPI_ATTR void VKAPI_CALL End(VkCommandBuffer) { events.push_back("end"); }
VKAPI_ATTR void VKAPI_CALL Dispatch(VkCommandBuffer, uint32_t x, uint32_t y, uint32_t z) {
    assert(x == 3 && y == 4 && z == 5); events.push_back("dispatch");
}
}
int main(int argc, char**) {
    using namespace Vulkan;
    using namespace ComputeDiagnostic;
    if (argc > 1) { unsetenv("SHADPS4_COMPUTE_DISPATCH_LABELS"); assert(!Enabled()); return 0; }
    setenv("SHADPS4_COMPUTE_DISPATCH_LABELS", "1", 1); assert(Enabled());
    auto& d = vk::detail::defaultDispatchLoaderDynamic;
    d.vkCmdBeginDebugUtilsLabelEXT = Begin; d.vkCmdEndDebugUtilsLabelEXT = End;
    d.vkCmdDispatch = Dispatch;
    const auto label = GuestLabel(0x123, false, {3,4,5}, {64,1,1});
    assert(label == "guest_cs_0000000000000123 direct groups=3x4x5 local=64x1x1");
    const auto indirect = GuestLabel(~uint64_t{}, true, {0,0,0}, {32,2,1});
    assert(indirect.find("indirect") != std::string::npos);
    assert(TilingLabel(false, 2, 32, 4, 3, 70) == "host_detiler mode=2 bits=32 mips=4 slices=3 groups=70");
    assert(TilingLabel(true, 2, 32, 1, 1, 70).starts_with("host_tiler "));
    const vk::CommandBuffer cb{reinterpret_cast<VkCommandBuffer>(1)};
    CommandList list;
    CommandRecorder r{cb, &list};
    { std::string ephemeral = label; Scope scope{r, ephemeral}; r.dispatch(3,4,5); ephemeral = "changed"; }
    assert(events.empty() && list.Size() == 3);
    list.Replay(cb); assert((events == std::vector<std::string>{label, "dispatch", "end"}));
    events.clear(); { Scope disabled{r, ""}; r.dispatch(3,4,5); }
    list.Replay(cb); assert((events == std::vector<std::string>{"dispatch"}));
    events.clear(); { Scope direct{CommandRecorder{cb}, label}; CommandRecorder{cb}.dispatch(3,4,5); }
    assert((events == std::vector<std::string>{label, "dispatch", "end"}));
    events.clear(); d.vkCmdBeginDebugUtilsLabelEXT = nullptr;
    { Scope unsupported{r, label}; r.dispatch(3,4,5); }
    list.Replay(cb); assert((events == std::vector<std::string>{"dispatch"}));
    return 0;
}
