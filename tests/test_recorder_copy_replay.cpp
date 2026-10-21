// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "video_core/renderer_vulkan/vk_command_recorder.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

void assert_fail_impl() {
    std::abort();
}

namespace Common {
std::string GetCurrentThreadName() {
    return "recorder-copy-replay-test";
}
} // namespace Common

namespace Common::Log {
std::unordered_map<std::string_view, std::shared_ptr<spdlog::logger>> ALL_LOGGERS;
} // namespace Common::Log

namespace {

template <typename T>
T Handle(std::uintptr_t value) {
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<T>(value);
    } else {
        return static_cast<T>(value);
    }
}

// One write as vkCmdPushDescriptorSetKHR received it, with the info arrays copied out.
struct ReceivedWrite {
    VkDescriptorType type{};
    uint32_t binding{};
    uint32_t count{};
    std::vector<VkDescriptorImageInfo> images;
    std::vector<VkDescriptorBufferInfo> buffers;
    std::vector<VkBufferView> texels;
};

std::vector<std::vector<ReceivedWrite>> pushes;

VKAPI_ATTR void VKAPI_CALL FakePushDescriptorSetKHR(VkCommandBuffer, VkPipelineBindPoint,
                                                    VkPipelineLayout, uint32_t,
                                                    uint32_t write_count,
                                                    const VkWriteDescriptorSet* writes) {
    std::vector<ReceivedWrite> push;
    for (uint32_t i = 0; i < write_count; ++i) {
        const VkWriteDescriptorSet& w = writes[i];
        ReceivedWrite received{};
        received.type = w.descriptorType;
        received.binding = w.dstBinding;
        received.count = w.descriptorCount;
        if (w.pImageInfo) {
            received.images.assign(w.pImageInfo, w.pImageInfo + w.descriptorCount);
        }
        if (w.pBufferInfo) {
            received.buffers.assign(w.pBufferInfo, w.pBufferInfo + w.descriptorCount);
        }
        if (w.pTexelBufferView) {
            received.texels.assign(w.pTexelBufferView, w.pTexelBufferView + w.descriptorCount);
        }
        push.push_back(std::move(received));
    }
    pushes.push_back(std::move(push));
}

#define CHECK(x)                                                                                \
    do {                                                                                        \
        if (!(x)) {                                                                             \
            std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #x);                \
            std::abort();                                                                       \
        }                                                                                       \
    } while (false)

} // namespace

int main() {
    vk::detail::defaultDispatchLoaderDynamic.vkCmdPushDescriptorSetKHR = FakePushDescriptorSetKHR;
    // The descriptor memo would skip repeated pushes; this test wants every push replayed.
    unsetenv("SHADPS4_DESCRIPTOR_PUSH_MEMO");
    unsetenv("SHADPS4_DYNAMIC_STATE_REUSE");
    unsetenv("SHADPS4_PUSH_CONSTANT_REUSE");

    const vk::CommandBuffer cb{Handle<VkCommandBuffer>(1)};
    const vk::PipelineLayout layout{Handle<VkPipelineLayout>(2)};

    // Caller-owned data: a storage buffer, an array of three image views, a texel buffer view and
    // a sampler, one write each. Writes 1 and 3 have counts that take the multi-element path.
    vk::DescriptorBufferInfo buffer_info{};
    buffer_info.buffer = Handle<VkBuffer>(10);
    buffer_info.offset = 16;
    buffer_info.range = 64;
    std::array<vk::DescriptorImageInfo, 3> image_infos{};
    for (uint32_t i = 0; i < image_infos.size(); ++i) {
        image_infos[i].imageView = Handle<VkImageView>(40 + i);
        image_infos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }
    vk::DescriptorImageInfo sampler_info{};
    sampler_info.sampler = Handle<VkSampler>(20);
    sampler_info.imageLayout = vk::ImageLayout::eGeneral;
    vk::BufferView texel = Handle<VkBufferView>(30);

    std::array<vk::WriteDescriptorSet, 4> writes{};
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
    writes[0].pBufferInfo = &buffer_info;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 3;
    writes[1].descriptorType = vk::DescriptorType::eSampledImage;
    writes[1].pImageInfo = image_infos.data();
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = vk::DescriptorType::eUniformTexelBuffer;
    writes[2].pTexelBufferView = &texel;
    writes[3].dstBinding = 3;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = vk::DescriptorType::eSampler;
    writes[3].pImageInfo = &sampler_info;

    {
        Vulkan::CommandList list;
        Vulkan::CommandRecorder recorder{cb, &list};
        recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, layout, 0, writes);

        // A recorded push owns its contents: the caller's data may change or go away after it.
        buffer_info = {};
        image_infos = {};
        sampler_info = {};
        texel = vk::BufferView{};
        writes = {};

        list.Replay(cb);
    }

    CHECK(pushes.size() == 1);
    const auto& push = pushes[0];
    CHECK(push.size() == 4);

    CHECK(push[0].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    CHECK(push[0].binding == 0);
    CHECK(push[0].count == 1);
    CHECK(push[0].buffers.size() == 1);
    CHECK(push[0].buffers[0].buffer == Handle<VkBuffer>(10));
    CHECK(push[0].buffers[0].offset == 16);
    CHECK(push[0].buffers[0].range == 64);
    CHECK(push[0].images.empty());
    CHECK(push[0].texels.empty());

    CHECK(push[1].type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    CHECK(push[1].binding == 1);
    CHECK(push[1].count == 3);
    CHECK(push[1].images.size() == 3);
    for (uint32_t i = 0; i < 3; ++i) {
        CHECK(push[1].images[i].imageView == Handle<VkImageView>(40 + i));
        CHECK(push[1].images[i].imageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    CHECK(push[1].buffers.empty());

    CHECK(push[2].type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
    CHECK(push[2].binding == 2);
    CHECK(push[2].texels.size() == 1);
    CHECK(push[2].texels[0] == Handle<VkBufferView>(30));

    CHECK(push[3].type == VK_DESCRIPTOR_TYPE_SAMPLER);
    CHECK(push[3].binding == 3);
    CHECK(push[3].images.size() == 1);
    CHECK(push[3].images[0].sampler == Handle<VkSampler>(20));
    CHECK(push[3].images[0].imageLayout == VK_IMAGE_LAYOUT_GENERAL);

    // Copy on its own: no copy for a null array or a zero count, and every element copied for
    // one element and for several, with the arena keeping the alignment of the type.
    {
        Vulkan::CommandList arena;
        CHECK(arena.Copy<int>(nullptr, 0) == nullptr);

        std::array<std::uint64_t, 3> values{1, 2, 3};
        const auto* one = arena.Copy(values.data(), 1);
        const auto* three = arena.Copy(values.data(), 3);
        values = {7, 8, 9}; // the copies must not follow the source
        CHECK(one[0] == 1);
        CHECK(three[0] == 1 && three[1] == 2 && three[2] == 3);
        CHECK(reinterpret_cast<std::uintptr_t>(one) % alignof(std::uint64_t) == 0);
        CHECK(reinterpret_cast<std::uintptr_t>(three) % alignof(std::uint64_t) == 0);
    }

    return 0;
}
