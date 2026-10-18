// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cassert>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "video_core/renderer_vulkan/vk_command_recorder.h"
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
void assert_fail_impl() { std::abort(); }
namespace Common { std::string GetCurrentThreadName() { return "rendering-recorder-test"; } }
namespace Common::Log { std::unordered_map<std::string_view, std::shared_ptr<spdlog::logger>> ALL_LOGGERS; }
namespace {
struct Snapshot {
    VkRenderingInfo info;
    std::vector<VkRenderingAttachmentInfo> colors;
    std::optional<VkRenderingAttachmentInfo> depth, stencil;
    explicit Snapshot(const VkRenderingInfo& source) : info(source) {
        if (source.colorAttachmentCount)
            colors.assign(source.pColorAttachments, source.pColorAttachments + source.colorAttachmentCount);
        if (source.pDepthAttachment) depth = *source.pDepthAttachment;
        if (source.pStencilAttachment) stencil = *source.pStencilAttachment;
        info.pColorAttachments = nullptr;
        info.pDepthAttachment = nullptr;
        info.pStencilAttachment = nullptr;
    }
    bool operator==(const Snapshot& other) const {
        if (std::memcmp(&info, &other.info, sizeof(info)) || colors.size() != other.colors.size() ||
            depth.has_value() != other.depth.has_value() || stencil.has_value() != other.stencil.has_value()) return false;
        for (size_t i = 0; i < colors.size(); ++i)
            if (std::memcmp(&colors[i], &other.colors[i], sizeof(colors[i]))) return false;
        return (!depth || !std::memcmp(&*depth, &*other.depth, sizeof(*depth))) &&
               (!stencil || !std::memcmp(&*stencil, &*other.stencil, sizeof(*stencil)));
    }
};
std::vector<Snapshot> observed;
VKAPI_ATTR void VKAPI_CALL Begin(VkCommandBuffer, const VkRenderingInfo* info) {
    observed.emplace_back(*info);
}
}
int main() {
    vk::detail::defaultDispatchLoaderDynamic.vkCmdBeginRendering = Begin;
    const vk::CommandBuffer cb{reinterpret_cast<VkCommandBuffer>(1)};
    Vulkan::CommandList list;
    Vulkan::CommandRecorder recorder{cb, &list};
    for (unsigned pass = 0; pass < 3; ++pass) {
        observed.clear();
        std::vector<Snapshot> expected;
        for (unsigned n = 0; n < 160; ++n) {
            std::array<vk::RenderingAttachmentInfo, 8> attachments{};
            for (unsigned i = 0; i < attachments.size(); ++i) {
                attachments[i].imageView = vk::ImageView{reinterpret_cast<VkImageView>(100 + n * 8 + i)};
                attachments[i].loadOp = i % 2 ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear;
                attachments[i].storeOp = vk::AttachmentStoreOp::eStore;
                attachments[i].clearValue.color.uint32[0] = n * 8 + i;
            }
            vk::RenderingInfo info{};
            info.renderArea.extent = vk::Extent2D{1280 + n, 720};
            info.layerCount = 1;
            info.colorAttachmentCount = n % 9;
            info.pColorAttachments = attachments.data();
            info.pDepthAttachment = n % 2 ? &attachments[6] : nullptr;
            info.pStencilAttachment = n % 3 ? &attachments[6] : nullptr;
            expected.emplace_back(static_cast<const VkRenderingInfo&>(info));
            recorder.beginRendering(info);
            std::memset(attachments.data(), 0x43, sizeof(attachments));
            std::array<unsigned char, 4096> junk{};
            for (unsigned j = 0; j < 4; ++j) {
                auto* copied = list.Copy(junk.data(), junk.size());
                list.Record([copied](vk::CommandBuffer) { assert(copied[0] == 0); });
            }
        }
        std::vector<unsigned char> large(100000, 0x25);
        auto* copied = list.Copy(large.data(), large.size());
        list.Record([copied](vk::CommandBuffer) { assert(copied[99999] == 0x25); });
        list.Replay(cb);
        assert(observed == expected && list.Empty());
    }
    std::array<vk::RenderingAttachmentInfo, 1> attachment{};
    vk::RenderingInfo direct_info{};
    direct_info.colorAttachmentCount = 1;
    direct_info.pColorAttachments = attachment.data();
    Vulkan::CommandRecorder direct{cb};
    const Snapshot expected{static_cast<const VkRenderingInfo&>(direct_info)};
    direct.beginRendering(direct_info);
    assert(observed.back() == expected);
}
