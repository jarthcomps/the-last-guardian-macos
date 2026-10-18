// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/recursive_lock.h"
#include "common/shared_first_mutex.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/page_manager.h"
#include "video_core/renderer_vulkan/fence_deferral.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/texture_cache/texture_cache.h"

namespace AmdGpu {
struct Liverpool;
}

namespace Core {
class MemoryManager;
}

namespace Vulkan {

class Scheduler;
class RenderState;
class GraphicsPipeline;

class Rasterizer {
public:
    explicit Rasterizer(const Instance& instance, Scheduler& scheduler,
                        AmdGpu::Liverpool* liverpool);
    ~Rasterizer();

    [[nodiscard]] Scheduler& GetScheduler() noexcept {
        return scheduler;
    }

    [[nodiscard]] VideoCore::BufferCache& GetBufferCache() noexcept {
        return buffer_cache;
    }

    [[nodiscard]] VideoCore::TextureCache& GetTextureCache() noexcept {
        return texture_cache;
    }

    void Draw(bool is_indexed, u32 index_offset = 0);
    void DrawIndirect(bool is_indexed, VAddr arg_address, u32 offset, u32 size, u32 max_count,
                      VAddr count_address, u16 vertex_sgpr_offset, u16 instance_sgpr_offset);

    void DispatchDirect();
    void DispatchIndirect(VAddr address, u32 offset, u32 size);

    void ScopeMarker(fmt::string_view fmt, fmt::format_args args, auto&& func) {
        if (host_markers_enabled) {
            ScopeMarkerBegin(fmt::vformat(fmt, args));
            func();
            ScopeMarkerEnd();
        } else {
            func();
        }
    }

    void ScopeMarkerBegin(const std::string_view& str, bool from_guest = false);
    void ScopeMarkerEnd(bool from_guest = false);
    void ScopedMarkerInsert(const std::string_view& str, bool from_guest = false);
    void ScopedMarkerInsertColor(const std::string_view& str, const u32 color,
                                 bool from_guest = false);

    void FillBuffer(VAddr address, u32 num_bytes, u32 value, bool is_gds);
    void CopyBuffer(VAddr dst, VAddr src, u32 num_bytes, bool dst_gds, bool src_gds);
    u32 ReadDataFromGds(u32 gsd_offset);
    bool InvalidateMemory(VAddr addr, u64 size);
    bool ReadMemory(VAddr addr, u64 size);
    void ProcessDownloadImages();

    /// Handles readbacks at an end-of-pipe fence and runs `signal` once their data is in guest
    /// memory. With async fences the GPU thread doesn't wait and `signal` runs on another thread;
    /// `must_sync` forces the blocking path (for fences whose signal needs the GPU thread).
    /// Records the readback copies and submission of every deferred fence, oldest first. The
    /// command processor also calls this when a ring yields on a wait or when it runs out of
    /// work: the guest may be blocked on a deferred fence with nothing else ending the pass.
    void FlushDeferredFences(FlushReason reason = FlushReason::Submit);
    /// A guest CS partial flush or AcquireMem (SHADPS4_HONOR_CS_FLUSH, fence_deferral.h).
    void GuestComputeFlush();
    void OnFence(Common::UniqueFunction<void>&& signal, VideoCore::FenceKind kind,
                 bool must_sync = false, bool interrupting = false);
    bool IsMapped(VAddr addr, u64 size);
    void MapMemory(VAddr addr, u64 size);
    void UnmapMemory(VAddr addr, u64 size);

    void CpSync();

    /// Readback stats: a guest memory write done by the command processor itself.
    void NoteCpGuestWrite(VAddr addr, u64 size) {
        buffer_cache.NoteCpGuestWrite(addr, size);
    }

    /// Orders all later GPU work after everything recorded so far, for a command stream wait
    /// that passed because its producer (a fence recorded at `producer_tick`) was already
    /// recorded, not because it finished. Returns whether a barrier was recorded.
    bool CommandStreamBarrier(u64 producer_tick);

    [[nodiscard]] u64 CurrentTick() const noexcept;

    u64 Flush();
    void Finish();
    void OnSubmit();

    PipelineCache& GetPipelineCache() {
        return pipeline_cache;
    }

    template <typename Func>
    void ForEachMappedRangeInRange(VAddr addr, u64 size, Func&& func) {
        const auto range = decltype(mapped_ranges)::interval_type::right_open(addr, addr + size);
        Common::RecursiveSharedLock lock{mapped_ranges_mutex};
        for (const auto& mapped_range : (mapped_ranges & range)) {
            func(mapped_range);
        }
    }

private:
    void ProcessReadbacksSync();
    void PrepareRenderState(const GraphicsPipeline* pipeline);
    RenderState BeginRendering(const GraphicsPipeline* pipeline);
    void Resolve();
    void DepthStencilCopy(bool is_depth, bool is_stencil);
    void EliminateFastClear();

    void UpdateDynamicState(const GraphicsPipeline* pipeline, bool is_indexed) const;
    void UpdateViewportScissorState() const;
    void UpdateDepthStencilState() const;
    void UpdatePrimitiveState(bool is_indexed) const;
    void UpdateRasterizationState() const;
    void UpdateColorBlendingState(const GraphicsPipeline* pipeline) const;

    bool FilterDraw();

    void BindBuffers(const Shader::Info& stage, Shader::Backend::Bindings& binding,
                     Shader::PushData& push_data);
    void BindTextures(const Shader::Info& stage, Shader::Backend::Bindings& binding);
    bool BindResources(const Pipeline* pipeline);

    void ResetBindings() {
        CommitHazards();
        for (auto& image_id : bound_images) {
            texture_cache.GetImage(image_id).binding = {};
        }
        bound_images.clear();
    }

    /// Readback stats: compares a draw or dispatch with the previous one.
    void NoteDrawReuse(const GraphicsPipeline* pipeline);
    void NoteDispatchReuse(const ComputePipeline* pipeline);
    /// Profiler only: how often a stage's resource sharps repeat the last binding of the same
    /// stage (sizes a skip-unchanged optimization for buffer and texture bindings).
    void NoteBindReuse(const Shader::Info& stage);

    bool IsComputeMetaClear(const Pipeline* pipeline);
    bool IsComputeImageCopy(const Pipeline* pipeline);
    bool IsComputeImageClear(const Pipeline* pipeline);

private:
    friend class VideoCore::BufferCache;

    const Instance& instance;
    Scheduler& scheduler;
    VideoCore::PageManager page_manager;
    VideoCore::BufferCache buffer_cache;
    VideoCore::TextureCache texture_cache;
    AmdGpu::Liverpool* liverpool;
    Core::MemoryManager* memory;
    boost::icl::interval_set<VAddr> mapped_ranges;
    Common::SharedFirstMutex mapped_ranges_mutex;
    PipelineCache pipeline_cache;
    const bool host_markers_enabled;
    const bool guest_markers_enabled;
    const bool readback_batching;
    const bool async_fences;
    /// SHADPS4_DEFER_FENCES_IN_PASS=1: fences arriving inside a render pass wait for the pass to
    /// end before their readback copies and submission are recorded (fence_deferral.h).
    const bool defer_fences_in_pass;
    /// SHADPS4_METAL_ENCODER_ORDERING=1: skip command stream barriers inside a render pass
    /// (fence_deferral.h, ShouldSkipCommandStreamBarrier).
    const bool trust_encoder_ordering;
    /// A draw of the open render pass used a shader that writes memory.
    bool pass_memory_writes{};
    /// SHADPS4_HONOR_CS_FLUSH=1 (fence_deferral.h, ShouldRecordGuestComputeBarrier).
    const bool honor_cs_flush;
    /// SHADPS4_SERIALIZE_DISPATCHES=1, diagnostics: a full barrier after every dispatch, so no two
    /// dispatches can overlap on the GPU. Tells GPU races between dispatches apart from others.
    const bool serialize_dispatches;
    void SerializeAfterDispatch();
    u64 last_guest_flush_work{~0ULL};
    DeferredFenceQueue deferred_fences;
    DeferralStats deferral_stats;
    u64 deferral_stats_last_ns{};
    void ReportDeferralStats();
    u64 recorded_work{};
    u64 last_barrier_work{~0ULL};

    /// rt_lookup_reuse_enabled: the register inputs of the last target lookup of each color slot
    /// (and the depth target, last entry) and the texture cache generation it was made at.
    struct CachedTarget {
        bool valid{};
        u64 generation{};
        u32 size{};
        std::array<u8, 256> inputs{};
    };
    std::array<CachedTarget, AmdGpu::NUM_COLOR_BUFFERS + 1> rt_cache{};
    const bool rt_reuse_enabled;
    std::array<std::vector<VideoCore::TextureCache::CachedImageLookup>, 8> texture_lookups{};
    const u32 encoder_debug;
    /// Encoder thread: fences without readbacks wait for all work recorded before them.
    const bool full_eop_fences;
    /// SHADPS4_EARLY_EOP=1: interrupting EOPs signal at the command processor (experiment).
    const bool early_eop;
    /// Fence kinds that get full fences (cp_full_fence_kinds).
    const u32 full_fence_kinds;

    /// Image ids bound as attachments of the open render pass.
    boost::container::static_vector<VideoCore::ImageId, AmdGpu::NUM_COLOR_BUFFERS + 1>
    PassImages() const;
    /// True when a stage of the pipeline writes buffers or images from shaders.
    static bool PipelineWritesMemory(const Pipeline* pipeline);
    /// The asynchronous fence path shared by direct and deferred fences.
    void ProcessAsyncFence(Common::UniqueFunction<void>&& signal, VideoCore::FenceKind kind,
                           bool interrupting = false);
    /// True if the inputs match the cached lookup; otherwise stores them for the next draw.
    template <typename... Ts>
    bool TargetInputsMatch(CachedTarget& cache, const Ts&... parts);

    struct ReuseState {
        const GraphicsPipeline* draw_pipeline{};
        const ComputePipeline* dispatch_pipeline{};
        std::array<u32, 0x400> ctx_regs{};
        std::vector<u8> rt_regs;
        std::vector<u8> scratch;
        std::array<AmdGpu::ShaderProgram, 6> programs{};
        AmdGpu::UserData cs_user_data{};
    };
    ReuseState reuse_state;

    struct BindSnapshot {
        u64 pgm_hash{};
        std::vector<AmdGpu::Buffer> buffers;
        std::vector<AmdGpu::Image> images;
        std::vector<AmdGpu::Sampler> samplers;
    };
    std::array<BindSnapshot, 8> bind_snapshots;

    using RenderTargetInfo = std::pair<VideoCore::ImageId, VideoCore::TextureCache::ImageDesc>;
    std::array<RenderTargetInfo, AmdGpu::NUM_COLOR_BUFFERS> cb_descs;
    std::pair<VideoCore::ImageId, VideoCore::TextureCache::ImageDesc> db_desc;
    boost::container::static_vector<vk::DescriptorImageInfo, Shader::NUM_IMAGES> image_infos;
    boost::container::static_vector<vk::DescriptorBufferInfo, Shader::NUM_BUFFERS> buffer_infos;
    boost::container::static_vector<VideoCore::ImageId, Shader::NUM_IMAGES> bound_images;

    u32 set_write_index{};
    Pipeline::DescriptorWrites set_writes;
    Pipeline::BufferBarriers buffer_barriers;
    /// Buffer ranges the draw or dispatch being recorded accesses, committed to each buffer's
    /// BufferHazardTracker once it is recorded (SHADPS4_PRECISE_BUFFER_HAZARDS).
    struct PendingHazard {
        VideoCore::Buffer* buffer;
        u64 begin;
        u64 end;
        bool is_write;
    };
    boost::container::small_vector<PendingHazard, 16> pending_hazards;
    bool binding_compute{};
    void CommitHazards();
    Shader::PushData push_data;

    using BufferBindingInfo = std::tuple<VideoCore::BufferId, AmdGpu::Buffer, u64>;
    boost::container::static_vector<BufferBindingInfo, Shader::NUM_BUFFERS> buffer_bindings;
    using ImageBindingInfo = std::pair<VideoCore::ImageId, VideoCore::TextureCache::ImageDesc>;
    boost::container::static_vector<ImageBindingInfo, Shader::NUM_IMAGES> image_bindings;
    bool fault_process_pending{};
    bool attachment_feedback_loop{};
};

} // namespace Vulkan
