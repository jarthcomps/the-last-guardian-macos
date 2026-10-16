// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

#include "common/assert.h"
#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/barrier_stats.h"

#ifdef MemoryBarrier
#pragma push_macro("MemoryBarrier")
#undef MemoryBarrier
#define SHADPS4_RESTORE_MEMORY_BARRIER
#endif

namespace Vulkan {

class CommandRecorder;
struct DescriptorPushMemoTestAccess;

/// Vulkan commands recorded as value-captured closures, replayed later into a command buffer.
/// Everything a command needs (arrays, structures with pointers, strings) is copied into the
/// list's arena, so the caller's data may change or go away right after recording.
class CommandList {
public:
    CommandList() {
        const char* enabled = std::getenv("SHADPS4_DESCRIPTOR_PUSH_MEMO");
        if (enabled && enabled[0] == '1' && enabled[1] == '\0') {
            descriptor_memo = std::make_unique<DescriptorMemo>();
            const char* diagnostics = std::getenv("SHADPS4_DESCRIPTOR_MEMO_DIAGNOSTICS");
            descriptor_memo_diagnostics = diagnostics && diagnostics[0] == '1' &&
                                          diagnostics[1] == '\0';
        }
    }
    CommandList(const CommandList&) = delete;
    CommandList& operator=(const CommandList&) = delete;

    [[nodiscard]] bool Empty() const noexcept {
        return head == nullptr;
    }

    /// Number of commands recorded since the last replay.
    [[nodiscard]] u64 Size() const noexcept {
        return num_commands;
    }

    /// Records a command. `func` is called with the command buffer at replay time.
    template <typename F>
    void Record(F&& func) {
        using Func = std::decay_t<F>;
        static_assert(std::is_trivially_destructible_v<Func>,
                      "recorded commands must only capture values and arena pointers");
        auto* command = new (Allocate(sizeof(CommandImpl<Func>), alignof(CommandImpl<Func>)))
            CommandImpl<Func>{std::forward<F>(func)};
        command->execute = [](Command* self, vk::CommandBuffer cmdbuf) {
            static_cast<CommandImpl<Func>*>(self)->func(cmdbuf);
        };
        Link(command);
    }

    /// Records a pipeline bind unless this list already has that pipeline bound at the bind point.
    /// The cache is deliberately local to this list and is cleared when it is replayed/reset.
    void BindPipeline(vk::PipelineBindPoint bind_point, vk::Pipeline pipeline) {
        size_t cache_index{};
        switch (bind_point) {
        case vk::PipelineBindPoint::eGraphics:
            cache_index = 0;
            break;
        case vk::PipelineBindPoint::eCompute:
            cache_index = 1;
            break;
        default:
            // Keep uncommon/extension bind points on the original, uncached path.
            Record([=](vk::CommandBuffer c) { c.bindPipeline(bind_point, pipeline); });
            return;
        }
        if (!pipeline) {
            pipeline_valid[cache_index] = false;
            Record([=](vk::CommandBuffer c) { c.bindPipeline(bind_point, pipeline); });
            return;
        }
        if (pipeline_valid[cache_index] && pipelines[cache_index] == pipeline) {
            return;
        }
        pipeline_valid[cache_index] = true;
        pipelines[cache_index] = pipeline;
        Record([=](vk::CommandBuffer c) { c.bindPipeline(bind_point, pipeline); });
    }

    /// Copies an array into the arena. The copy lives until the next replay.
    template <typename T>
    [[nodiscard]] T* Copy(const T* data, size_t count) {
        static_assert(std::is_trivially_copyable_v<T>);
        if (count == 0) {
            return nullptr;
        }
        auto* dst = static_cast<T*>(Allocate(sizeof(T) * count, alignof(T)));
        std::memcpy(dst, data, sizeof(T) * count);
        return dst;
    }

    /// Copies a null-terminated string into the arena.
    [[nodiscard]] const char* CopyString(const char* str) {
        if (!str) {
            return nullptr;
        }
        return Copy(str, std::strlen(str) + 1);
    }

    /// Replays every recorded command into `cmdbuf` in order and empties the list.
    void Replay(vk::CommandBuffer cmdbuf) {
        for (Command* command = head; command; command = command->next) {
            command->execute(command, cmdbuf);
        }
        Reset();
    }

private:
    friend class CommandRecorder;
    friend struct DescriptorPushMemoTestAccess;

    struct DescriptorValue {
        VkDescriptorType type{};
        VkSampler sampler{};
        VkImageView image_view{};
        VkImageLayout image_layout{};
        VkBuffer buffer{};
        VkDeviceSize offset{};
        VkDeviceSize range{};
        VkBufferView texel_view{};
        bool valid{};
    };

    struct DescriptorBank {
        std::array<DescriptorValue, 128> bindings{};
        VkPipelineLayout layout{};
        bool layout_valid{};
    };

    struct DescriptorMemo {
        std::array<DescriptorBank, 2> banks{};
    };

    static size_t DescriptorBankIndex(vk::PipelineBindPoint bind_point) {
        switch (bind_point) {
        case vk::PipelineBindPoint::eGraphics:
            return 0;
        case vk::PipelineBindPoint::eCompute:
            return 1;
        default:
            return 2;
        }
    }

    static bool SameDescriptorValue(const DescriptorValue& lhs, const DescriptorValue& rhs) {
        if (!lhs.valid || lhs.type != rhs.type) {
            return false;
        }
        switch (lhs.type) {
        case VK_DESCRIPTOR_TYPE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            return lhs.sampler == rhs.sampler && lhs.image_view == rhs.image_view &&
                   lhs.image_layout == rhs.image_layout;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            return lhs.buffer == rhs.buffer && lhs.offset == rhs.offset && lhs.range == rhs.range;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            return lhs.texel_view == rhs.texel_view;
        default:
            return false;
        }
    }

    static bool ReadDescriptorValue(const vk::WriteDescriptorSet& write, DescriptorValue& value) {
        value = {};
        value.type = static_cast<VkDescriptorType>(write.descriptorType);
        switch (value.type) {
        case VK_DESCRIPTOR_TYPE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            if (!write.pImageInfo || write.pBufferInfo || write.pTexelBufferView) {
                return false;
            }
            value.sampler = static_cast<VkSampler>(write.pImageInfo->sampler);
            value.image_view = static_cast<VkImageView>(write.pImageInfo->imageView);
            value.image_layout = static_cast<VkImageLayout>(write.pImageInfo->imageLayout);
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            if (!write.pBufferInfo || write.pImageInfo || write.pTexelBufferView) {
                return false;
            }
            value.buffer = static_cast<VkBuffer>(write.pBufferInfo->buffer);
            value.offset = write.pBufferInfo->offset;
            value.range = write.pBufferInfo->range;
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            if (!write.pTexelBufferView || write.pImageInfo || write.pBufferInfo) {
                return false;
            }
            value.texel_view = static_cast<VkBufferView>(*write.pTexelBufferView);
            break;
        default:
            return false;
        }
        value.valid = true;
        return true;
    }

    static void Invalidate(DescriptorBank& bank) {
        bank.layout_valid = false;
        for (auto& binding : bank.bindings) {
            binding.valid = false;
        }
    }

    void InvalidateDescriptorMemo(vk::PipelineBindPoint bind_point) {
        if (!descriptor_memo) {
            return;
        }
        const size_t index = DescriptorBankIndex(bind_point);
        if (index == 2) {
            for (auto& bank : descriptor_memo->banks) {
                Invalidate(bank);
            }
        } else {
            Invalidate(descriptor_memo->banks[index]);
        }
    }

    void NoteDescriptorSkip(u32 binding, vk::PipelineBindPoint bind_point) const {
        if (!descriptor_memo_diagnostics) {
            return;
        }
        static std::atomic<u32> diagnostic_count{};
        if (diagnostic_count.fetch_add(1, std::memory_order_relaxed) < 8) {
            std::fprintf(stderr, "shadPS4 descriptor push memo skipped bindpoint=%u binding=%u\n",
                         static_cast<u32>(bind_point), binding);
        }
    }

    bool MemoPushDescriptorSet(vk::PipelineBindPoint bind_point, vk::PipelineLayout layout,
                               u32 set, u32 count,
                               const vk::WriteDescriptorSet* writes) {
        if (!descriptor_memo) {
            return false;
        }
        const size_t bank_index = DescriptorBankIndex(bind_point);
        if (bank_index == 2 || set != 0 || count == 0 || count > 128 || !writes || !layout) {
            InvalidateDescriptorMemo(bind_point);
            return false;
        }

        DescriptorBank& bank = descriptor_memo->banks[bank_index];
        if (!bank.layout_valid || bank.layout != static_cast<VkPipelineLayout>(layout)) {
            Invalidate(bank);
            bank.layout = static_cast<VkPipelineLayout>(layout);
            bank.layout_valid = true;
        }

        // Preflight the whole call before touching cached state. Unsupported writes retain the
        // original command and invalidate this bank, so a later supported write cannot skip over
        // state changed by an extension descriptor or an unknown write shape.
        for (u32 i = 0; i < count; ++i) {
            DescriptorValue value{};
            if (writes[i].sType != vk::StructureType::eWriteDescriptorSet || writes[i].pNext ||
                writes[i].dstArrayElement != 0 || writes[i].descriptorCount != 1 ||
                writes[i].dstBinding >= bank.bindings.size() ||
                !ReadDescriptorValue(writes[i], value)) {
                Invalidate(bank);
                return false;
            }
        }

        std::array<bool, 128> changed{};
        u32 changed_count{};
        for (u32 i = 0; i < count; ++i) {
            DescriptorValue value{};
            ReadDescriptorValue(writes[i], value);
            const u32 binding = writes[i].dstBinding;
            if (!SameDescriptorValue(bank.bindings[binding], value)) {
                changed[i] = true;
                ++changed_count;
            } else {
                NoteDescriptorSkip(binding, bind_point);
            }
            bank.bindings[binding] = value;
        }
        if (changed_count == 0) {
            return true;
        }

        auto* filtered = Copy(writes, count);
        u32 output_count{};
        for (u32 i = 0; i < count; ++i) {
            if (!changed[i]) {
                continue;
            }
            const auto& src = writes[i];
            auto& dst = filtered[output_count++];
            dst = src;
            dst.pImageInfo = nullptr;
            dst.pBufferInfo = nullptr;
            dst.pTexelBufferView = nullptr;
            switch (static_cast<VkDescriptorType>(src.descriptorType)) {
            case VK_DESCRIPTOR_TYPE_SAMPLER:
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                dst.pImageInfo = Copy(src.pImageInfo, 1);
                break;
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                dst.pBufferInfo = Copy(src.pBufferInfo, 1);
                break;
            case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                dst.pTexelBufferView = Copy(src.pTexelBufferView, 1);
                break;
            default:
                Invalidate(bank);
                return false;
            }
        }
        Record([=](vk::CommandBuffer c) {
            c.pushDescriptorSetKHR(bind_point, layout, set, output_count, filtered);
        });
        return true;
    }

    struct Command {
        void (*execute)(Command*, vk::CommandBuffer){};
        Command* next{};
    };

    template <typename Func>
    struct CommandImpl : Command {
        explicit CommandImpl(Func&& func_) : func{std::move(func_)} {}
        explicit CommandImpl(const Func& func_) : func{func_} {}
        Func func;
    };

    static constexpr size_t ChunkSize = 256 * 1024;

    void Link(Command* command) {
        if (tail) {
            tail->next = command;
        } else {
            head = command;
        }
        tail = command;
        ++num_commands;
    }

    void* Allocate(size_t size, size_t align) {
        if (size + align > ChunkSize / 4) {
            // Rare large data (e.g. an image upload with many regions): a block of its own,
            // freed at the next replay.
            auto& block = large_blocks.emplace_back(std::make_unique<u8[]>(size + align));
            const auto base = reinterpret_cast<uintptr_t>(block.get());
            return reinterpret_cast<void*>((base + align - 1) & ~(uintptr_t{align} - 1));
        }
        while (true) {
            if (chunk_index < chunks.size()) {
                const size_t aligned = (offset + align - 1) & ~(align - 1);
                if (aligned + size <= ChunkSize) {
                    offset = aligned + size;
                    return chunks[chunk_index].get() + aligned;
                }
                ++chunk_index;
                offset = 0;
                continue;
            }
            chunks.push_back(std::make_unique<u8[]>(ChunkSize));
        }
    }

    void Reset() {
        head = nullptr;
        tail = nullptr;
        num_commands = 0;
        chunk_index = 0;
        offset = 0;
        large_blocks.clear();
        pipeline_valid.fill(false);
        pipelines.fill(vk::Pipeline{});
        if (descriptor_memo) {
            for (auto& bank : descriptor_memo->banks) {
                Invalidate(bank);
            }
        }
    }

    std::vector<std::unique_ptr<u8[]>> chunks;
    std::vector<std::unique_ptr<u8[]>> large_blocks;
    size_t chunk_index{};
    size_t offset{};
    Command* head{};
    Command* tail{};
    u64 num_commands{};
    std::array<vk::Pipeline, 2> pipelines{};
    std::array<bool, 2> pipeline_valid{};
    std::unique_ptr<DescriptorMemo> descriptor_memo;
    bool descriptor_memo_diagnostics{};
};

/// What the scheduler hands out instead of a raw command buffer. In direct mode every call goes
/// straight to the command buffer (the old behavior). In recording mode calls are appended to a
/// CommandList with all their data copied, and replayed at submit time.
class CommandRecorder {
public:
    CommandRecorder() = default;
    CommandRecorder(vk::CommandBuffer cmdbuf_) : cmdbuf{cmdbuf_} {}
    CommandRecorder(vk::CommandBuffer cmdbuf_, CommandList* list_) : cmdbuf{cmdbuf_}, list{list_} {}

    explicit operator bool() const noexcept {
        return list != nullptr || static_cast<bool>(cmdbuf);
    }

    [[nodiscard]] bool IsRecording() const noexcept {
        return list != nullptr;
    }

    // ---------------------------------------------------------------- binding and state

    void bindPipeline(vk::PipelineBindPoint bind_point, vk::Pipeline pipeline) const {
        if (list) {
            list->BindPipeline(bind_point, pipeline);
        } else {
            cmdbuf.bindPipeline(bind_point, pipeline);
        }
    }

    void bindIndexBuffer(vk::Buffer buffer, vk::DeviceSize offset, vk::IndexType type) const {
        Do([=](vk::CommandBuffer c) { c.bindIndexBuffer(buffer, offset, type); });
    }

    void bindVertexBuffers(u32 first, u32 count, const vk::Buffer* buffers,
                           const vk::DeviceSize* offsets) const {
        if (!list) {
            cmdbuf.bindVertexBuffers(first, count, buffers, offsets);
            return;
        }
        const auto* b = list->Copy(buffers, count);
        const auto* o = list->Copy(offsets, count);
        list->Record([=](vk::CommandBuffer c) { c.bindVertexBuffers(first, count, b, o); });
    }

    void bindVertexBuffers2(u32 first, u32 count, const vk::Buffer* buffers,
                            const vk::DeviceSize* offsets, const vk::DeviceSize* sizes,
                            const vk::DeviceSize* strides) const {
        if (!list) {
            cmdbuf.bindVertexBuffers2(first, count, buffers, offsets, sizes, strides);
            return;
        }
        const auto* b = list->Copy(buffers, count);
        const auto* o = list->Copy(offsets, count);
        const auto* s = sizes ? list->Copy(sizes, count) : nullptr;
        const auto* st = strides ? list->Copy(strides, count) : nullptr;
        list->Record([=](vk::CommandBuffer c) { c.bindVertexBuffers2(first, count, b, o, s, st); });
    }

    void setVertexInputEXT(
        vk::ArrayProxy<const vk::VertexInputBindingDescription2EXT> bindings,
        vk::ArrayProxy<const vk::VertexInputAttributeDescription2EXT> attributes) const {
        if (!list) {
            cmdbuf.setVertexInputEXT(bindings, attributes);
            return;
        }
        const u32 nb = bindings.size();
        const u32 na = attributes.size();
        const auto* b = list->Copy(bindings.data(), nb);
        const auto* a = list->Copy(attributes.data(), na);
        list->Record([=](vk::CommandBuffer c) { c.setVertexInputEXT(nb, b, na, a); });
    }

    void pushConstants(vk::PipelineLayout layout, vk::ShaderStageFlags stages, u32 offset, u32 size,
                       const void* values) const {
        if (!list) {
            cmdbuf.pushConstants(layout, stages, offset, size, values);
            return;
        }
        const auto* v = list->Copy(static_cast<const u8*>(values), size);
        list->Record(
            [=](vk::CommandBuffer c) { c.pushConstants(layout, stages, offset, size, v); });
    }

    template <typename T>
    void pushConstants(vk::PipelineLayout layout, vk::ShaderStageFlags stages, u32 offset,
                       vk::ArrayProxy<const T> values) const {
        pushConstants(layout, stages, offset, static_cast<u32>(values.size() * sizeof(T)),
                      values.data());
    }

    void pushDescriptorSetKHR(vk::PipelineBindPoint bind_point, vk::PipelineLayout layout, u32 set,
                              vk::ArrayProxy<const vk::WriteDescriptorSet> writes) const {
        if (!list) {
            cmdbuf.pushDescriptorSetKHR(bind_point, layout, set, writes);
            return;
        }
        const u32 count = writes.size();
        if (list->MemoPushDescriptorSet(bind_point, layout, set, count, writes.data())) {
            return;
        }
        auto* w = list->Copy(writes.data(), count);
        for (u32 i = 0; i < count; ++i) {
            ASSERT_MSG(!w[i].pNext, "Recorded descriptor writes can't have a pNext chain");
            w[i].pImageInfo =
                list->Copy(w[i].pImageInfo, w[i].pImageInfo ? w[i].descriptorCount : 0);
            w[i].pBufferInfo =
                list->Copy(w[i].pBufferInfo, w[i].pBufferInfo ? w[i].descriptorCount : 0);
            w[i].pTexelBufferView =
                list->Copy(w[i].pTexelBufferView, w[i].pTexelBufferView ? w[i].descriptorCount : 0);
        }
        list->Record([=](vk::CommandBuffer c) {
            c.pushDescriptorSetKHR(bind_point, layout, set, count, w);
        });
    }

    void bindDescriptorSets(vk::PipelineBindPoint bind_point, vk::PipelineLayout layout,
                            u32 first_set, vk::ArrayProxy<const vk::DescriptorSet> sets,
                            vk::ArrayProxy<const u32> dynamic_offsets) const {
        if (!list) {
            cmdbuf.bindDescriptorSets(bind_point, layout, first_set, sets, dynamic_offsets);
            return;
        }
        list->InvalidateDescriptorMemo(bind_point);
        const u32 ns = sets.size();
        const u32 no = dynamic_offsets.size();
        const auto* s = list->Copy(sets.data(), ns);
        const auto* o = list->Copy(dynamic_offsets.data(), no);
        list->Record([=](vk::CommandBuffer c) {
            c.bindDescriptorSets(bind_point, layout, first_set, ns, s, no, o);
        });
    }

    // ---------------------------------------------------------------- dynamic state

    void setViewport(u32 first, vk::ArrayProxy<const vk::Viewport> viewports) const {
        if (!list) {
            cmdbuf.setViewport(first, viewports);
            return;
        }
        const u32 n = viewports.size();
        const auto* v = list->Copy(viewports.data(), n);
        list->Record([=](vk::CommandBuffer c) { c.setViewport(first, n, v); });
    }

    void setScissor(u32 first, vk::ArrayProxy<const vk::Rect2D> scissors) const {
        if (!list) {
            cmdbuf.setScissor(first, scissors);
            return;
        }
        const u32 n = scissors.size();
        const auto* s = list->Copy(scissors.data(), n);
        list->Record([=](vk::CommandBuffer c) { c.setScissor(first, n, s); });
    }

    void setViewportWithCount(vk::ArrayProxy<const vk::Viewport> viewports) const {
        if (!list) {
            cmdbuf.setViewportWithCount(viewports);
            return;
        }
        const u32 n = viewports.size();
        const auto* v = list->Copy(viewports.data(), n);
        list->Record([=](vk::CommandBuffer c) { c.setViewportWithCount(n, v); });
    }

    void setScissorWithCount(vk::ArrayProxy<const vk::Rect2D> scissors) const {
        if (!list) {
            cmdbuf.setScissorWithCount(scissors);
            return;
        }
        const u32 n = scissors.size();
        const auto* s = list->Copy(scissors.data(), n);
        list->Record([=](vk::CommandBuffer c) { c.setScissorWithCount(n, s); });
    }

    void setDepthTestEnable(bool enable) const {
        Do([=](vk::CommandBuffer c) { c.setDepthTestEnable(enable); });
    }
    void setDepthWriteEnable(bool enable) const {
        Do([=](vk::CommandBuffer c) { c.setDepthWriteEnable(enable); });
    }
    void setDepthCompareOp(vk::CompareOp op) const {
        Do([=](vk::CommandBuffer c) { c.setDepthCompareOp(op); });
    }
    void setDepthBoundsTestEnable(bool enable) const {
        Do([=](vk::CommandBuffer c) { c.setDepthBoundsTestEnable(enable); });
    }
    void setDepthBounds(float min, float max) const {
        Do([=](vk::CommandBuffer c) { c.setDepthBounds(min, max); });
    }
    void setDepthBiasEnable(bool enable) const {
        Do([=](vk::CommandBuffer c) { c.setDepthBiasEnable(enable); });
    }
    void setDepthBias(float constant, float clamp, float slope) const {
        Do([=](vk::CommandBuffer c) { c.setDepthBias(constant, clamp, slope); });
    }
    void setStencilTestEnable(bool enable) const {
        Do([=](vk::CommandBuffer c) { c.setStencilTestEnable(enable); });
    }
    void setStencilOp(vk::StencilFaceFlags faces, vk::StencilOp fail, vk::StencilOp pass,
                      vk::StencilOp depth_fail, vk::CompareOp compare) const {
        Do([=](vk::CommandBuffer c) { c.setStencilOp(faces, fail, pass, depth_fail, compare); });
    }
    void setStencilReference(vk::StencilFaceFlags faces, u32 reference) const {
        Do([=](vk::CommandBuffer c) { c.setStencilReference(faces, reference); });
    }
    void setStencilWriteMask(vk::StencilFaceFlags faces, u32 mask) const {
        Do([=](vk::CommandBuffer c) { c.setStencilWriteMask(faces, mask); });
    }
    void setStencilCompareMask(vk::StencilFaceFlags faces, u32 mask) const {
        Do([=](vk::CommandBuffer c) { c.setStencilCompareMask(faces, mask); });
    }
    void setPrimitiveRestartEnable(bool enable) const {
        Do([=](vk::CommandBuffer c) { c.setPrimitiveRestartEnable(enable); });
    }
    void setRasterizerDiscardEnable(bool enable) const {
        Do([=](vk::CommandBuffer c) { c.setRasterizerDiscardEnable(enable); });
    }
    void setCullMode(vk::CullModeFlags mode) const {
        Do([=](vk::CommandBuffer c) { c.setCullMode(mode); });
    }
    void setFrontFace(vk::FrontFace face) const {
        Do([=](vk::CommandBuffer c) { c.setFrontFace(face); });
    }
    void setLineWidth(float width) const {
        Do([=](vk::CommandBuffer c) { c.setLineWidth(width); });
    }
    void setBlendConstants(const float constants[4]) const {
        const std::array<float, 4> values{constants[0], constants[1], constants[2], constants[3]};
        Do([=](vk::CommandBuffer c) { c.setBlendConstants(values.data()); });
    }
    void setColorWriteMaskEXT(u32 first,
                              vk::ArrayProxy<const vk::ColorComponentFlags> masks) const {
        if (!list) {
            cmdbuf.setColorWriteMaskEXT(first, masks);
            return;
        }
        const u32 n = masks.size();
        const auto* m = list->Copy(masks.data(), n);
        list->Record([=](vk::CommandBuffer c) { c.setColorWriteMaskEXT(first, n, m); });
    }
    void setAttachmentFeedbackLoopEnableEXT(vk::ImageAspectFlags aspects) const {
        Do([=](vk::CommandBuffer c) { c.setAttachmentFeedbackLoopEnableEXT(aspects); });
    }

    // ---------------------------------------------------------------- rendering and work

    void beginRendering(const vk::RenderingInfo& info) const {
        if (!list) {
            cmdbuf.beginRendering(info);
            return;
        }
        ASSERT_MSG(!info.pNext, "Recorded rendering info can't have a pNext chain");
        auto* copy = list->Copy(&info, 1);
        copy->pColorAttachments = list->Copy(info.pColorAttachments, info.colorAttachmentCount);
        copy->pDepthAttachment =
            info.pDepthAttachment ? list->Copy(info.pDepthAttachment, 1) : nullptr;
        copy->pStencilAttachment =
            info.pStencilAttachment ? list->Copy(info.pStencilAttachment, 1) : nullptr;
        list->Record([=](vk::CommandBuffer c) { c.beginRendering(*copy); });
    }

    void endRendering() const {
        Do([](vk::CommandBuffer c) { c.endRendering(); });
    }

    void draw(u32 vertices, u32 instances, u32 first_vertex, u32 first_instance) const {
        Do([=](vk::CommandBuffer c) { c.draw(vertices, instances, first_vertex, first_instance); });
    }

    void drawIndexed(u32 indices, u32 instances, u32 first_index, s32 vertex_offset,
                     u32 first_instance) const {
        Do([=](vk::CommandBuffer c) {
            c.drawIndexed(indices, instances, first_index, vertex_offset, first_instance);
        });
    }

    void drawIndirect(vk::Buffer buffer, vk::DeviceSize offset, u32 count, u32 stride) const {
        Do([=](vk::CommandBuffer c) { c.drawIndirect(buffer, offset, count, stride); });
    }

    void drawIndexedIndirect(vk::Buffer buffer, vk::DeviceSize offset, u32 count,
                             u32 stride) const {
        Do([=](vk::CommandBuffer c) { c.drawIndexedIndirect(buffer, offset, count, stride); });
    }

    void drawIndirectCount(vk::Buffer buffer, vk::DeviceSize offset, vk::Buffer count_buffer,
                           vk::DeviceSize count_offset, u32 max_count, u32 stride) const {
        Do([=](vk::CommandBuffer c) {
            c.drawIndirectCount(buffer, offset, count_buffer, count_offset, max_count, stride);
        });
    }

    void drawIndexedIndirectCount(vk::Buffer buffer, vk::DeviceSize offset, vk::Buffer count_buffer,
                                  vk::DeviceSize count_offset, u32 max_count, u32 stride) const {
        Do([=](vk::CommandBuffer c) {
            c.drawIndexedIndirectCount(buffer, offset, count_buffer, count_offset, max_count,
                                       stride);
        });
    }

    void dispatch(u32 x, u32 y, u32 z) const {
        Do([=](vk::CommandBuffer c) { c.dispatch(x, y, z); });
    }

    void dispatchIndirect(vk::Buffer buffer, vk::DeviceSize offset) const {
        Do([=](vk::CommandBuffer c) { c.dispatchIndirect(buffer, offset); });
    }

    // ---------------------------------------------------------------- barriers

    void pipelineBarrier2(const vk::DependencyInfo& info) const {
        if (!BarrierStats::OnBarrier()) {
            return;
        }
        if (!list) {
            cmdbuf.pipelineBarrier2(info);
            return;
        }
        ASSERT_MSG(!info.pNext, "Recorded dependency info can't have a pNext chain");
        auto* copy = list->Copy(&info, 1);
        copy->pMemoryBarriers = list->Copy(info.pMemoryBarriers, info.memoryBarrierCount);
        copy->pBufferMemoryBarriers =
            list->Copy(info.pBufferMemoryBarriers, info.bufferMemoryBarrierCount);
        copy->pImageMemoryBarriers =
            list->Copy(info.pImageMemoryBarriers, info.imageMemoryBarrierCount);
        list->Record([=](vk::CommandBuffer c) { c.pipelineBarrier2(*copy); });
    }

    void pipelineBarrier(vk::PipelineStageFlags src, vk::PipelineStageFlags dst,
                         vk::DependencyFlags flags,
                         vk::ArrayProxy<const vk::MemoryBarrier> memory_barriers,
                         vk::ArrayProxy<const vk::BufferMemoryBarrier> buffer_barriers,
                         vk::ArrayProxy<const vk::ImageMemoryBarrier> image_barriers) const {
        if (!BarrierStats::OnBarrier()) {
            return;
        }
        if (!list) {
            cmdbuf.pipelineBarrier(src, dst, flags, memory_barriers, buffer_barriers,
                                   image_barriers);
            return;
        }
        const u32 nm = memory_barriers.size();
        const u32 nb = buffer_barriers.size();
        const u32 ni = image_barriers.size();
        const auto* m = list->Copy(memory_barriers.data(), nm);
        const auto* b = list->Copy(buffer_barriers.data(), nb);
        const auto* i = list->Copy(image_barriers.data(), ni);
        list->Record(
            [=](vk::CommandBuffer c) { c.pipelineBarrier(src, dst, flags, nm, m, nb, b, ni, i); });
    }

    // ---------------------------------------------------------------- transfers

    void copyBuffer(vk::Buffer src, vk::Buffer dst,
                    vk::ArrayProxy<const vk::BufferCopy> regions) const {
        if (!list) {
            cmdbuf.copyBuffer(src, dst, regions);
            return;
        }
        const u32 n = regions.size();
        const auto* r = list->Copy(regions.data(), n);
        list->Record([=](vk::CommandBuffer c) { c.copyBuffer(src, dst, n, r); });
    }

    void copyImage(vk::Image src, vk::ImageLayout src_layout, vk::Image dst,
                   vk::ImageLayout dst_layout, vk::ArrayProxy<const vk::ImageCopy> regions) const {
        if (!list) {
            cmdbuf.copyImage(src, src_layout, dst, dst_layout, regions);
            return;
        }
        const u32 n = regions.size();
        const auto* r = list->Copy(regions.data(), n);
        list->Record(
            [=](vk::CommandBuffer c) { c.copyImage(src, src_layout, dst, dst_layout, n, r); });
    }

    void copyBufferToImage(vk::Buffer src, vk::Image dst, vk::ImageLayout dst_layout,
                           vk::ArrayProxy<const vk::BufferImageCopy> regions) const {
        if (!list) {
            cmdbuf.copyBufferToImage(src, dst, dst_layout, regions);
            return;
        }
        const u32 n = regions.size();
        const auto* r = list->Copy(regions.data(), n);
        list->Record([=](vk::CommandBuffer c) { c.copyBufferToImage(src, dst, dst_layout, n, r); });
    }

    void copyImageToBuffer(vk::Image src, vk::ImageLayout src_layout, vk::Buffer dst,
                           vk::ArrayProxy<const vk::BufferImageCopy> regions) const {
        if (!list) {
            cmdbuf.copyImageToBuffer(src, src_layout, dst, regions);
            return;
        }
        const u32 n = regions.size();
        const auto* r = list->Copy(regions.data(), n);
        list->Record([=](vk::CommandBuffer c) { c.copyImageToBuffer(src, src_layout, dst, n, r); });
    }

    void resolveImage(vk::Image src, vk::ImageLayout src_layout, vk::Image dst,
                      vk::ImageLayout dst_layout,
                      vk::ArrayProxy<const vk::ImageResolve> regions) const {
        if (!list) {
            cmdbuf.resolveImage(src, src_layout, dst, dst_layout, regions);
            return;
        }
        const u32 n = regions.size();
        const auto* r = list->Copy(regions.data(), n);
        list->Record(
            [=](vk::CommandBuffer c) { c.resolveImage(src, src_layout, dst, dst_layout, n, r); });
    }

    void clearColorImage(vk::Image image, vk::ImageLayout layout, const vk::ClearColorValue& color,
                         vk::ArrayProxy<const vk::ImageSubresourceRange> ranges) const {
        if (!list) {
            cmdbuf.clearColorImage(image, layout, color, ranges);
            return;
        }
        const u32 n = ranges.size();
        const auto* r = list->Copy(ranges.data(), n);
        const vk::ClearColorValue value = color;
        list->Record([=](vk::CommandBuffer c) { c.clearColorImage(image, layout, &value, n, r); });
    }

    void fillBuffer(vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize size, u32 data) const {
        Do([=](vk::CommandBuffer c) { c.fillBuffer(buffer, offset, size, data); });
    }

    // ---------------------------------------------------------------- queries and markers

    void resetQueryPool(vk::QueryPool pool, u32 first, u32 count) const {
        Do([=](vk::CommandBuffer c) { c.resetQueryPool(pool, first, count); });
    }

    void writeTimestamp(vk::PipelineStageFlagBits stage, vk::QueryPool pool, u32 query) const {
        Do([=](vk::CommandBuffer c) { c.writeTimestamp(stage, pool, query); });
    }

    void beginDebugUtilsLabelEXT(const vk::DebugUtilsLabelEXT& label) const {
        if (!list) {
            cmdbuf.beginDebugUtilsLabelEXT(label);
            return;
        }
        auto copy = label;
        copy.pLabelName = list->CopyString(label.pLabelName);
        list->Record([=](vk::CommandBuffer c) { c.beginDebugUtilsLabelEXT(copy); });
    }

    void insertDebugUtilsLabelEXT(const vk::DebugUtilsLabelEXT& label) const {
        if (!list) {
            cmdbuf.insertDebugUtilsLabelEXT(label);
            return;
        }
        auto copy = label;
        copy.pLabelName = list->CopyString(label.pLabelName);
        list->Record([=](vk::CommandBuffer c) { c.insertDebugUtilsLabelEXT(copy); });
    }

    void endDebugUtilsLabelEXT() const {
        Do([](vk::CommandBuffer c) { c.endDebugUtilsLabelEXT(); });
    }

private:
    template <typename F>
    void Do(F&& func) const {
        if (list) {
            list->Record(std::forward<F>(func));
        } else {
            func(cmdbuf);
        }
    }

    vk::CommandBuffer cmdbuf{};
    CommandList* list{};
};

} // namespace Vulkan

#ifdef SHADPS4_RESTORE_MEMORY_BARRIER
#pragma pop_macro("MemoryBarrier")
#undef SHADPS4_RESTORE_MEMORY_BARRIER
#endif
