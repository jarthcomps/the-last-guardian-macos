// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "common/types.h"
#include "common/unique_function.h"

namespace Vulkan {

/// A guest fence whose readback copies and submission wait for the open render pass to end.
///
/// On a tile based GPU every render pass break stores and reloads all attachments. The Last
/// Guardian signals about 200 fences per frame, most of them in the middle of a render pass, and
/// each one with readback copies or a submission ended the pass (readback copies cannot be
/// recorded inside a render pass and a submission closes the command buffer). Deferring that work
/// to the next point where the pass ends anyway keeps the pass whole. Deferred fences are flushed,
/// in order, before anything that could change the memory their copies read: a dispatch, a draw
/// whose shaders write buffers or images, a command stream barrier, a synchronous fence, a submit.
struct DeferredFence {
    Common::UniqueFunction<void> signal;
    u32 kind{};
};

/// Decides whether a fence may be deferred. `downloads_pass_attachment` is true when a pending
/// image readback targets an attachment of the open pass: later draws of that pass would then
/// change what the guest reads, so the copy has to be recorded now.
constexpr bool ShouldDeferFence(bool enabled, bool async_fences, bool rendering, bool must_sync,
                                bool submit_end_kind, bool downloads_pass_attachment) noexcept {
    return enabled && async_fences && rendering && !must_sync && !submit_end_kind &&
           !downloads_pass_attachment;
}

/// SHADPS4_METAL_ENCODER_ORDERING=1: a command stream barrier (a graphics ring wait that passed
/// on a recorded fence) is skipped while a render pass is open. KosmicKrisp serializes Metal
/// encoders, so every command recorded before the open render encoder already completed before
/// it, and draws inside one encoder run in order. The barrier only matters when a draw of the open
/// pass wrote memory through shaders, which later work in the pass may read.
constexpr bool ShouldSkipCommandStreamBarrier(bool trust_encoder_ordering, bool rendering,
                                              bool pass_memory_writes) noexcept {
    return trust_encoder_ordering && rendering && !pass_memory_writes;
}

/// SHADPS4_HONOR_CS_FLUSH=1: a guest CS partial flush (EventWrite) or AcquireMem asks for the
/// compute work before it to finish before later work starts. Upstream ignores both. A compute
/// barrier is recorded when work was recorded since the previous one; inside a render pass none
/// is needed, because a dispatch after the flush ends the pass anyway and KosmicKrisp orders
/// encoders.
constexpr bool ShouldRecordGuestComputeBarrier(bool enabled, bool rendering,
                                               bool work_since_last_barrier) noexcept {
    return enabled && !rendering && work_since_last_barrier;
}

/// Why deferred fences were flushed (SHADPS4_DEFER_FENCES_IN_PASS diagnostics).
enum class FlushReason : u32 {
    PassChange,  ///< a draw with another render target set
    MemoryWrite, ///< a draw whose shaders write memory
    Dispatch,    ///< a dispatch or a handled compute shader
    Barrier,     ///< a command stream barrier
    Submit,      ///< submit, flush or finish of the command processor
    Fence,       ///< a fence that could not be deferred
    Idle,        ///< the command processor ran out of work
    Yield,       ///< a ring yielded on a wait
    Count,
};

/// Counters printed once per second as "DEFER_STATS" when deferral is enabled.
struct DeferralStats {
    u64 fences{};           ///< asynchronous fences seen
    u64 in_pass{};          ///< of which arrived while a render pass was open
    u64 deferred{};         ///< of which were deferred
    u64 kept_attachment{};  ///< in a pass but a pending readback targets an attachment
    u64 barriers_skipped{}; ///< command stream barriers skipped inside a render pass
    u64 barriers_kept_memory_writes{}; ///< in a pass, kept because a draw of the pass wrote memory
    u64 cp_syncs_skipped{};            ///< PfpSyncMe syncs skipped inside a render pass
    u64 dma_copies_in_pass{};          ///< DmaData copies recorded while a render pass was open
    u64 dma_fills_in_pass{};           ///< DmaData fills recorded while a render pass was open
    u64 draw_barriers_skipped{};       ///< draws whose buffer barriers were skipped inside a pass
    u64 write_after_write_barriers{};  ///< buffer barriers added between consecutive writes
    u64 read_after_read_skipped{};     ///< buffer barriers skipped between two reads
    u64 image_read_after_read_skipped{}; ///< image transitions skipped between two reads
    u64 precise_hazards_elided{};        ///< dispatch buffer barriers left out (precise hazards)
    u64 guest_cs_flushes{};            ///< guest CS partial flushes and AcquireMem packets seen
    u64 guest_cs_flush_barriers{};     ///< of which recorded a compute barrier
    std::array<u64, static_cast<size_t>(FlushReason::Count)> flushes{}; ///< flush calls that found deferred fences

    std::string Format(double seconds) const {
        static constexpr const char* names[] = {"pass_change", "memory_write", "dispatch",
                                                "barrier",     "submit",       "fence",
                                                "idle",        "yield"};
        std::string line = "DEFER_STATS dt=" + std::to_string(seconds) + "s fences=" +
                           std::to_string(fences) + " in_pass=" + std::to_string(in_pass) +
                           " deferred=" + std::to_string(deferred) +
                           " kept_attachment=" + std::to_string(kept_attachment) +
                           " barriers_skipped=" + std::to_string(barriers_skipped) +
                           " barriers_kept_memory_writes=" +
                           std::to_string(barriers_kept_memory_writes) +
                           " cp_syncs_skipped=" + std::to_string(cp_syncs_skipped) +
                           " dma_copies_in_pass=" + std::to_string(dma_copies_in_pass) +
                           " dma_fills_in_pass=" + std::to_string(dma_fills_in_pass) +
                           " draw_barriers_skipped=" + std::to_string(draw_barriers_skipped) +
                           " write_after_write_barriers=" +
                           std::to_string(write_after_write_barriers) +
                           " read_after_read_skipped=" + std::to_string(read_after_read_skipped) +
                           " image_read_after_read_skipped=" +
                           std::to_string(image_read_after_read_skipped) +
                           " precise_hazards_elided=" + std::to_string(precise_hazards_elided) +
                           " guest_cs_flushes=" + std::to_string(guest_cs_flushes) +
                           " guest_cs_flush_barriers=" + std::to_string(guest_cs_flush_barriers) +
                           " flushes:";
        for (size_t i = 0; i < flushes.size(); ++i) {
            line += ' ';
            line += names[i];
            line += '=';
            line += std::to_string(flushes[i]);
        }
        line += '\n';
        return line;
    }
};

/// Deferred fences in arrival order.
class DeferredFenceQueue {
public:
    void Push(Common::UniqueFunction<void>&& signal, u32 kind) {
        fences.push_back({std::move(signal), kind});
    }

    [[nodiscard]] bool Empty() const noexcept {
        return fences.empty();
    }

    [[nodiscard]] size_t Size() const noexcept {
        return fences.size();
    }

    /// Hands over every deferred fence, oldest first, leaving the queue empty.
    std::vector<DeferredFence> Take() {
        return std::exchange(fences, {});
    }

private:
    std::vector<DeferredFence> fences;
};

} // namespace Vulkan
