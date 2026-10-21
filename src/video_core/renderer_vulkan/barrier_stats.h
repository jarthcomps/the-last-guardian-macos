// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace Vulkan::BarrierStats {

/// Which engine step recorded a pipeline barrier (SHADPS4_BARRIER_STATS=1, diagnostics). On
/// Metal every barrier recorded in a compute encoder waits for all earlier work of the encoder,
/// so these counts show which steps serialize the GPU.
enum class Source : std::uint8_t {
    Other,
    Resources,      ///< buffer hazards of a draw or dispatch (Pipeline::BindResources)
    Upload,         ///< CPU data copied into a buffer (BufferCache::SynchronizeBuffer)
    Download,       ///< GPU data copied back to the CPU (BufferCache::RecordDownloadCopies)
    CopyBuffer,     ///< guest DMA copy (BufferCache::CopyBuffer)
    CopyBufferGpu,  ///< buffer to buffer copy on the GPU (BufferCache::CopyBufferGpu)
    JoinOverlap,    ///< buffer merge (BufferCache::JoinOverlap)
    WriteData,      ///< small inline write (BufferCache::WriteDataBuffer)
    Fill,           ///< buffer fill (Buffer::Fill)
    ImageTransit,   ///< image layout/access transition (Image::Transit)
    ImageUpload,    ///< Image::Upload
    ImageDownload,  ///< Image::Download
    ImageCopy,      ///< Image::CopyImageWithBuffer
    ImageRefresh,   ///< TextureCache::RefreshImage
    CommandStream,  ///< guest command stream barrier (Rasterizer::CommandStreamBarrier)
    Serialize,      ///< Rasterizer::SerializeAfterDispatch (diagnostic switch)
    FaultBuffer,    ///< FaultManager::ProcessFaultBuffer
    Count,
};

constexpr std::array<std::string_view, static_cast<std::size_t>(Source::Count)> Names = {
    "other",      "resources",   "upload",       "download",       "copy_buffer",
    "copy_gpu",   "join",        "write_data",   "fill",           "image_transit",
    "image_upload", "image_download", "image_copy", "image_refresh", "command_stream",
    "serialize",  "fault_buffer",
};

inline thread_local Source current = Source::Other;

/// Full GPU barriers recorded by the command processor so far (see BufferHazardTracker). Only
/// barriers recorded on the command processor thread outside a render pass count: KosmicKrisp
/// turns them into waits for all earlier work of the compute encoder, and ends the encoder with
/// a full barrier anyway when none is open.
inline std::atomic<std::uint64_t> sync_epoch{0};
/// Set on the command processor thread.
inline thread_local bool sync_thread = false;
/// The command processor scheduler's "render pass open" flag.
inline const bool* rendering = nullptr;

inline std::uint64_t SyncEpoch() {
    return sync_epoch.load(std::memory_order_relaxed);
}

/// A full barrier (or the end of an encoder) was recorded on the calling thread.
inline void OnFullSync() {
    if (sync_thread) {
        sync_epoch.fetch_add(1, std::memory_order_relaxed);
    }
}

/// Attributes the barriers recorded while it lives to `source`.
class Scope {
public:
    explicit Scope(Source source) noexcept : prev{current} {
        current = source;
    }
    ~Scope() {
        current = prev;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Source prev;
};

struct Counters {
    std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(Source::Count)> counts{};
    std::atomic<std::uint64_t> last_report_ns{};

    void Add(Source source) {
        counts[static_cast<std::size_t>(source)].fetch_add(1, std::memory_order_relaxed);
    }

    /// The counts since the previous report as one line once a second passed, empty otherwise.
    std::string TakeReport(std::uint64_t now_ns) {
        std::uint64_t last = last_report_ns.load(std::memory_order_relaxed);
        if (last == 0) {
            last_report_ns.store(now_ns, std::memory_order_relaxed);
            return {};
        }
        if (now_ns - last < 1'000'000'000ULL ||
            !last_report_ns.compare_exchange_strong(last, now_ns)) {
            return {};
        }
        std::string line = "BARRIER_STATS";
        std::uint64_t total = 0;
        for (std::size_t i = 0; i < counts.size(); ++i) {
            const std::uint64_t n = counts[i].exchange(0, std::memory_order_relaxed);
            total += n;
            if (n != 0) {
                line += " ";
                line += Names[i];
                line += "=" + std::to_string(n);
            }
        }
        line += " total=" + std::to_string(total) + "\n";
        return line;
    }
};

/// Parses a comma separated list of source names into a bit mask (unknown names are ignored).
inline std::uint32_t ParseSourceMask(std::string_view list) {
    std::uint32_t mask = 0;
    while (!list.empty()) {
        const std::size_t comma = list.find(',');
        const std::string_view name = list.substr(0, comma);
        for (std::size_t i = 0; i < Names.size(); ++i) {
            if (Names[i] == name) {
                mask |= 1u << i;
            }
        }
        list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
    }
    return mask;
}

/// SHADPS4_DROP_BARRIERS=upload,resources,... (diagnostic, breaks synchronization): barriers of
/// those sources are not recorded, to measure what each source costs the GPU.
inline std::uint32_t DropMask() {
    static const std::uint32_t mask = [] {
        const char* value = std::getenv("SHADPS4_DROP_BARRIERS");
        return value != nullptr ? ParseSourceMask(value) : 0u;
    }();
    return mask;
}

inline bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("SHADPS4_BARRIER_STATS");
        return value != nullptr && std::string_view{value} == "1";
    }();
    return enabled;
}

/// Whether a Scope has any effect. Only the statistics (Enabled) and SHADPS4_DROP_BARRIERS
/// (DropMask) read the source of a barrier, so without either a Scope can be skipped.
[[nodiscard]] constexpr bool ScopeNeeded(bool stats_enabled, std::uint32_t drop_mask) noexcept {
    return stats_enabled || drop_mask != 0;
}

inline Counters& Global() {
    static Counters counters;
    return counters;
}

/// Called for every barrier before it is recorded; returns false when it must be dropped
/// (SHADPS4_DROP_BARRIERS).
inline bool OnBarrier() {
    const bool keep = (DropMask() & (1u << static_cast<std::uint32_t>(current))) == 0;
    if (keep && (rendering == nullptr || !*rendering)) {
        OnFullSync();
    }
    if (!Enabled()) {
        return keep;
    }
    auto& counters = Global();
    counters.Add(current);
    const auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    const std::string report = counters.TakeReport(now);
    if (!report.empty()) {
        std::fputs(report.c_str(), stderr);
        std::fflush(stderr);
    }
    return keep;
}

} // namespace Vulkan::BarrierStats
