// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdint>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "video_core/renderer_vulkan/vk_command_recorder.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

void assert_fail_impl() {
    std::abort();
}

namespace Common {
std::string GetCurrentThreadName() {
    return "descriptor-push-memo-test";
}
} // namespace Common

namespace Common::Log {
std::unordered_map<std::string_view, std::shared_ptr<spdlog::logger>> ALL_LOGGERS;
} // namespace Common::Log

namespace {

template <typename Handle>
Handle FakeHandle(std::uintptr_t value) {
    if constexpr (std::is_pointer_v<Handle>) {
        return reinterpret_cast<Handle>(value);
    } else {
        return static_cast<Handle>(value);
    }
}

template <typename Handle>
std::uintptr_t HandleValue(Handle value) {
    if constexpr (std::is_pointer_v<Handle>) {
        return reinterpret_cast<std::uintptr_t>(value);
    } else {
        return static_cast<std::uintptr_t>(value);
    }
}

vk::PipelineLayout Layout(std::uintptr_t value) {
    return vk::PipelineLayout{FakeHandle<VkPipelineLayout>(value)};
}

vk::CommandBuffer CommandBuffer(std::uintptr_t value = 1) {
    return vk::CommandBuffer{FakeHandle<VkCommandBuffer>(value)};
}

struct SavedDescriptor {
    VkStructureType s_type{};
    VkDescriptorType type{};
    std::uint32_t binding{};
    std::uint32_t array_element{};
    std::uint32_t count{};
    std::uintptr_t sampler{};
    std::uintptr_t image_view{};
    VkImageLayout image_layout{};
    std::uintptr_t buffer{};
    VkDeviceSize offset{};
    VkDeviceSize range{};
    std::uintptr_t texel_view{};
};

struct PushCall {
    VkPipelineBindPoint bind_point{};
    VkPipelineLayout layout{};
    std::uint32_t set{};
    std::vector<bool> pnext_present;
    std::vector<SavedDescriptor> writes;
};

std::vector<PushCall> push_calls;
std::vector<VkPipelineBindPoint> bind_set_calls;

VKAPI_ATTR void VKAPI_CALL FakePushDescriptorSetKHR(VkCommandBuffer,
                                                     VkPipelineBindPoint bind_point,
                                                     VkPipelineLayout layout, std::uint32_t set,
                                                     std::uint32_t count,
                                                     const VkWriteDescriptorSet* writes) {
    PushCall call{bind_point, layout, set, {}, {}};
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto& write = writes[i];
        call.pnext_present.push_back(write.pNext != nullptr);
        SavedDescriptor saved{};
        saved.s_type = write.sType;
        saved.type = write.descriptorType;
        saved.binding = write.dstBinding;
        saved.array_element = write.dstArrayElement;
        saved.count = write.descriptorCount;
        switch (write.descriptorType) {
        case VK_DESCRIPTOR_TYPE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            if (write.pImageInfo) {
                saved.sampler = HandleValue(write.pImageInfo->sampler);
                saved.image_view = HandleValue(write.pImageInfo->imageView);
                saved.image_layout = write.pImageInfo->imageLayout;
            }
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            if (write.pBufferInfo) {
                saved.buffer = HandleValue(write.pBufferInfo->buffer);
                saved.offset = write.pBufferInfo->offset;
                saved.range = write.pBufferInfo->range;
            }
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            if (write.pTexelBufferView) {
                saved.texel_view = HandleValue(*write.pTexelBufferView);
            }
            break;
        default:
            break;
        }
        call.writes.push_back(saved);
    }
    push_calls.push_back(std::move(call));
}

VKAPI_ATTR void VKAPI_CALL FakeBindDescriptorSets(VkCommandBuffer,
                                                   VkPipelineBindPoint bind_point,
                                                   VkPipelineLayout, std::uint32_t,
                                                   std::uint32_t, const VkDescriptorSet*,
                                                   std::uint32_t, const std::uint32_t*) {
    bind_set_calls.push_back(bind_point);
}

vk::WriteDescriptorSet ImageWrite(u32 binding, std::uintptr_t sampler,
                                  std::uintptr_t image_view,
                                  vk::DescriptorType type = vk::DescriptorType::eCombinedImageSampler,
                                  const vk::DescriptorImageInfo* info = nullptr) {
    static thread_local vk::DescriptorImageInfo owned_info{};
    owned_info = {vk::Sampler{FakeHandle<VkSampler>(sampler)},
                  vk::ImageView{FakeHandle<VkImageView>(image_view)},
                  vk::ImageLayout::eShaderReadOnlyOptimal};
    vk::WriteDescriptorSet write{};
    write.sType = vk::StructureType::eWriteDescriptorSet;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pImageInfo = info ? info : &owned_info;
    return write;
}

vk::WriteDescriptorSet BufferWrite(u32 binding, std::uintptr_t buffer, VkDeviceSize offset,
                                   VkDeviceSize range = 64) {
    static thread_local vk::DescriptorBufferInfo info{};
    info = {vk::Buffer{FakeHandle<VkBuffer>(buffer)}, offset, range};
    vk::WriteDescriptorSet write{};
    write.sType = vk::StructureType::eWriteDescriptorSet;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eUniformBuffer;
    write.pBufferInfo = &info;
    return write;
}

int Check(bool condition, int code) {
    return condition ? 0 : code;
}

#define REQUIRE(condition, code) do { if (const int failed = Check((condition), (code)); failed) return failed; } while (false)

void EnableMemo(bool enabled = true) {
    if (enabled) {
        setenv("SHADPS4_DESCRIPTOR_PUSH_MEMO", "1", 1);
    } else {
        unsetenv("SHADPS4_DESCRIPTOR_PUSH_MEMO");
    }
    unsetenv("SHADPS4_DESCRIPTOR_MEMO_DIAGNOSTICS");
}

void ClearCalls() {
    push_calls.clear();
    bind_set_calls.clear();
}

} // namespace

namespace Vulkan {

struct DescriptorPushMemoTestAccess {
    static bool Try(CommandList& list, vk::PipelineBindPoint bind_point,
                    vk::PipelineLayout layout, u32 set,
                    const vk::WriteDescriptorSet* write) {
        return list.MemoPushDescriptorSet(bind_point, layout, set, 1, write);
    }

    static bool IsBindingCached(const CommandList& list, size_t bank, u32 binding) {
        return list.descriptor_memo && list.descriptor_memo->banks[bank].bindings[binding].valid;
    }
};

} // namespace Vulkan

int main() {
    auto& dispatcher = vk::detail::defaultDispatchLoaderDynamic;
    dispatcher.vkCmdPushDescriptorSetKHR = FakePushDescriptorSetKHR;
    dispatcher.vkCmdBindDescriptorSets = FakeBindDescriptorSets;
    const auto command_buffer = CommandBuffer();

    // The actual recorder owns descriptor values through replay, even if the caller mutates its
    // temporary arrays after the push call has returned.
    EnableMemo();
    Vulkan::CommandList captured;
    Vulkan::CommandRecorder recorder{command_buffer, &captured};
    vk::DescriptorImageInfo image{vk::Sampler{FakeHandle<VkSampler>(11)},
                                  vk::ImageView{FakeHandle<VkImageView>(12)},
                                  vk::ImageLayout::eShaderReadOnlyOptimal};
    auto image_write = ImageWrite(3, 0, 0, vk::DescriptorType::eCombinedImageSampler, &image);
    auto buffer_write = BufferWrite(4, 21, 0);
    std::array<vk::WriteDescriptorSet, 2> initial{image_write, buffer_write};
    recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(1), 0, initial);
    image.sampler = vk::Sampler{FakeHandle<VkSampler>(99)};
    image.sampler = vk::Sampler{FakeHandle<VkSampler>(11)};
    auto changed_buffer = BufferWrite(4, 21, 16);
    std::array<vk::WriteDescriptorSet, 2> partial{image_write, changed_buffer};
    recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(1), 0, partial);
    image.sampler = vk::Sampler{FakeHandle<VkSampler>(99)};
    const_cast<vk::DescriptorBufferInfo*>(changed_buffer.pBufferInfo)->offset = 48;
    const auto before_replay = push_calls.size();
    captured.Replay(command_buffer);
    REQUIRE(before_replay == 0 && push_calls.size() == 2, 1);
    REQUIRE(push_calls[0].writes.size() == 2 && push_calls[1].writes.size() == 1, 2);
    REQUIRE(push_calls[0].writes[0].sampler == 11 && push_calls[0].writes[1].offset == 0, 3);
    REQUIRE(push_calls[1].writes[0].s_type == VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET &&
                push_calls[1].writes[0].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER &&
                push_calls[1].writes[0].binding == 4 &&
                push_calls[1].writes[0].array_element == 0 &&
                push_calls[1].writes[0].count == 1 && push_calls[1].writes[0].buffer == 21 &&
                push_calls[1].writes[0].offset == 16,
            4);

    // An all-unchanged push is omitted, while its bank remains valid for the following partial
    // buffer update. The memo is scoped independently to graphics and compute.
    ClearCalls();
    Vulkan::CommandList banks;
    Vulkan::CommandRecorder bank_recorder{command_buffer, &banks};
    auto gfx = ImageWrite(7, 31, 32);
    bank_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(2), 0, gfx);
    bank_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(2), 0, gfx);
    auto compute = ImageWrite(7, 31, 32);
    bank_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, Layout(2), 0, compute);
    auto compute_changed = ImageWrite(7, 31, 33);
    bank_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, Layout(2), 0,
                                       compute_changed);
    banks.Replay(command_buffer);
    REQUIRE(push_calls.size() == 3, 5);
    REQUIRE(push_calls[0].bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS &&
                push_calls[1].bind_point == VK_PIPELINE_BIND_POINT_COMPUTE &&
                push_calls[2].writes[0].image_view == 33,
            6);

    // A different layout, external descriptor-set bind, and Replay each invalidate the affected
    // memo bank, so a subsequent identical value is passed through again.
    ClearCalls();
    Vulkan::CommandList invalidations;
    Vulkan::CommandRecorder invalidator{command_buffer, &invalidations};
    invalidator.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(3), 0, gfx);
    invalidator.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(4), 0, gfx);
    invalidator.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(4), 0, gfx);
    const std::array<vk::DescriptorSet, 1> sets{vk::DescriptorSet{FakeHandle<VkDescriptorSet>(1)}};
    invalidator.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, Layout(4), 0, sets, {});
    invalidator.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(4), 0, gfx);
    invalidator.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, vk::PipelineLayout{}, 0,
                                     gfx);
    invalidator.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(4), 0, gfx);
    invalidations.Replay(command_buffer);
    REQUIRE(push_calls.size() == 5 && bind_set_calls.size() == 1, 7);
    invalidator.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(4), 0, gfx);
    invalidations.Replay(command_buffer);
    REQUIRE(push_calls.size() == 6, 8);

    // Unsupported set, array element/count, pNext, null pointer, and descriptor type are forwarded
    // intact and invalidate cache state rather than allowing stale suppression afterward.
    ClearCalls();
    Vulkan::CommandList fallback;
    Vulkan::CommandRecorder fallback_recorder{command_buffer, &fallback};
    auto stable = ImageWrite(9, 51, 52);
    fallback_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(5), 0, stable);
    fallback_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(5), 0, stable);
    auto other_set = ImageWrite(9, 51, 52);
    fallback_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(5), 1, other_set);
    fallback_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(5), 0, stable);
    auto array_write = ImageWrite(9, 51, 52);
    array_write.dstArrayElement = 1;
    fallback_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(5), 0,
                                           array_write);
    fallback_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(5), 0, stable);
    auto multi_write = ImageWrite(9, 51, 52);
    multi_write.descriptorCount = 2;
    const std::array<vk::DescriptorImageInfo, 2> multi_info{
        vk::DescriptorImageInfo{vk::Sampler{FakeHandle<VkSampler>(51)},
                                vk::ImageView{FakeHandle<VkImageView>(52)},
                                vk::ImageLayout::eShaderReadOnlyOptimal},
        vk::DescriptorImageInfo{vk::Sampler{FakeHandle<VkSampler>(53)},
                                vk::ImageView{FakeHandle<VkImageView>(54)},
                                vk::ImageLayout::eShaderReadOnlyOptimal}};
    multi_write.pImageInfo = multi_info.data();
    fallback_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(5), 0,
                                           multi_write);
    fallback_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(5), 0, stable);
    auto null_write = ImageWrite(9, 51, 52);
    null_write.pImageInfo = nullptr;
    fallback_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(5), 0,
                                           null_write);
    fallback_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(5), 0, stable);
    auto unknown_type = stable;
    unknown_type.descriptorType = static_cast<vk::DescriptorType>(1000);
    fallback_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(5), 0,
                                           unknown_type);
    fallback_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(5), 0, stable);
    fallback.Replay(command_buffer);
    REQUIRE(push_calls.size() == 11, 9);
    REQUIRE(push_calls[1].set == 1 && push_calls[3].writes[0].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
                push_calls[5].writes[0].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            10);

    // Unsupported pNext and bindpoint invalidate conservatively; duplicate writes keep their
    // input order through filtering.
    ClearCalls();
    Vulkan::CommandList edge_cases;
    Vulkan::CommandRecorder edge_recorder{command_buffer, &edge_cases};
    vk::DescriptorImageInfo duplicate_info_a{
        vk::Sampler{FakeHandle<VkSampler>(61)}, vk::ImageView{FakeHandle<VkImageView>(62)},
        vk::ImageLayout::eShaderReadOnlyOptimal};
    vk::DescriptorImageInfo duplicate_info_b{
        vk::Sampler{FakeHandle<VkSampler>(63)}, vk::ImageView{FakeHandle<VkImageView>(64)},
        vk::ImageLayout::eShaderReadOnlyOptimal};
    auto first_duplicate = ImageWrite(10, 0, 0,
                                      vk::DescriptorType::eCombinedImageSampler,
                                      &duplicate_info_a);
    auto second_duplicate = ImageWrite(10, 0, 0,
                                       vk::DescriptorType::eCombinedImageSampler,
                                       &duplicate_info_b);
    std::array<vk::WriteDescriptorSet, 2> duplicate_writes{first_duplicate, second_duplicate};
    edge_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(6), 0,
                                       duplicate_writes);
    edge_recorder.pushDescriptorSetKHR(static_cast<vk::PipelineBindPoint>(99), Layout(6), 0,
                                       second_duplicate);
    edge_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(6), 0,
                                       second_duplicate);
    int pnext_marker{};
    auto pnext_write = second_duplicate;
    pnext_write.pNext = &pnext_marker;
    REQUIRE(Vulkan::DescriptorPushMemoTestAccess::IsBindingCached(edge_cases, 0, 10), 15);
    REQUIRE(!Vulkan::DescriptorPushMemoTestAccess::Try(edge_cases,
                                                       vk::PipelineBindPoint::eGraphics,
                                                       Layout(6), 0, &pnext_write),
            16);
    REQUIRE(!Vulkan::DescriptorPushMemoTestAccess::IsBindingCached(edge_cases, 0, 10), 17);
    edge_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(6), 0,
                                       second_duplicate);
    edge_cases.Replay(command_buffer);
    REQUIRE(push_calls.size() == 4, 11);
    REQUIRE(push_calls[0].writes.size() == 2 && push_calls[0].writes[0].image_view == 62 &&
                push_calls[0].writes[1].image_view == 64 &&
                push_calls[3].writes[0].image_view == 64,
            12);

    // Memo-disabled recording and direct mode both preserve every call exactly as before.
    ClearCalls();
    EnableMemo(false);
    Vulkan::CommandList disabled;
    Vulkan::CommandRecorder disabled_recorder{command_buffer, &disabled};
    disabled_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(7), 0, gfx);
    disabled_recorder.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(7), 0, gfx);
    disabled.Replay(command_buffer);
    REQUIRE(push_calls.size() == 2, 13);
    ClearCalls();
    EnableMemo();
    Vulkan::CommandRecorder direct{command_buffer};
    direct.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(7), 0, gfx);
    direct.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, Layout(7), 0, gfx);
    REQUIRE(push_calls.size() == 2, 14);
    return 0;
}
