// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstdint>
#include "video_core/buffer_cache/region_manager.h"

// Keep the RegionManager state machine real while isolating its host-memory side effect. The
// standalone test has no mapped guest memory to protect; the PageManager template callbacks are
// therefore no-ops, while RegionManager still updates its CPU/writeable masks normally.
namespace VideoCore {
struct PageManager::Impl {};

PageManager::PageManager(Vulkan::Rasterizer*) : impl{std::make_unique<Impl>()} {}
PageManager::~PageManager() = default;

template <>
void PageManager::UpdatePageWatchersForRegion<true, false>(VAddr, RegionBits&) const {
}
template <>
void PageManager::UpdatePageWatchersForRegion<false, false>(VAddr, RegionBits&) const {
}
} // namespace VideoCore

namespace {

using namespace VideoCore;

constexpr VAddr CpuBase = 0x10000000;
constexpr size_t Page = 7;
constexpr VAddr PageAddress = CpuBase + Page * TRACKER_BYTES_PER_PAGE;

struct UploadResult {
    std::array<unsigned, 2> ranges{};
    unsigned count{};
};

UploadResult Upload(RegionManager& manager, bool keep_hot) {
    UploadResult result;
    manager.ForEachModifiedRange<Type::CPU, true>(
        PageAddress, TRACKER_BYTES_PER_PAGE,
        [&](VAddr address, size_t size) {
            if (result.count < result.ranges.size() && address == PageAddress &&
                size == TRACKER_BYTES_PER_PAGE) {
                result.ranges[result.count] = 1;
            }
            ++result.count;
        },
        keep_hot);
    return result;
}

bool IsOnePageUpload(const UploadResult& result) {
    return result.count == 1 && result.ranges[0] == 1;
}

void MarkCpuDirty(RegionManager& manager) {
    manager.ChangeRegionState<Type::CPU, true>(PageAddress, TRACKER_BYTES_PER_PAGE);
}

bool TestDisabledOptionKeepsOriginalClearBehavior() {
    PageManager tracker{nullptr};
    RegionManager manager(&tracker, CpuBase);
    MarkCpuDirty(manager);

    const auto upload = Upload(manager, true);
    return IsOnePageUpload(upload) &&
           !manager.IsRegionModified<Type::CPU>(Page * TRACKER_BYTES_PER_PAGE,
                                                TRACKER_BYTES_PER_PAGE) &&
           manager.NoteCpuWrite(PageAddress, TRACKER_BYTES_PER_PAGE) == 0;
}

bool TestThresholdAndRepeatedReadUploads() {
    PageManager tracker{nullptr};
    RegionManager manager(&tracker, CpuBase);
    manager.SetHotPagesEnabled(true);
    MarkCpuDirty(manager);

    // A refault is only counted after an upload has re-protected the page. The first two
    // rewrites therefore take the normal clear-and-reprotect path.
    for (unsigned rewrite = 1; rewrite <= 2; ++rewrite) {
        if (!IsOnePageUpload(Upload(manager, true))) {
            return false;
        }
        if (manager.IsRegionModified<Type::CPU>(Page * TRACKER_BYTES_PER_PAGE,
                                                TRACKER_BYTES_PER_PAGE)) {
            return false;
        }
        if (manager.NoteCpuWrite(PageAddress, TRACKER_BYTES_PER_PAGE) != 0) {
            return false;
        }
        MarkCpuDirty(manager);
    }

    if (!IsOnePageUpload(Upload(manager, true)) ||
        manager.NoteCpuWrite(PageAddress, TRACKER_BYTES_PER_PAGE) != 1) {
        return false;
    }
    MarkCpuDirty(manager);

    // Once hot, an upload for GPU reading must continue to report the page while preserving
    // its CPU-dirty state, so the next use uploads the newer bytes too.
    for (unsigned use = 0; use < 2; ++use) {
        if (!IsOnePageUpload(Upload(manager, true)) ||
            !manager.IsRegionModified<Type::CPU>(Page * TRACKER_BYTES_PER_PAGE,
                                                 TRACKER_BYTES_PER_PAGE)) {
            return false;
        }
    }
    return true;
}

bool TestDecayHalvesRefaultHistory() {
    PageManager tracker{nullptr};
    RegionManager manager(&tracker, CpuBase);
    manager.SetHotPagesEnabled(true);
    MarkCpuDirty(manager);

    // Build three observed write-after-upload refaults and leave the page hot/dirty.
    for (unsigned rewrite = 0; rewrite < 3; ++rewrite) {
        if (!IsOnePageUpload(Upload(manager, true))) {
            return false;
        }
        const u32 newly_hot = manager.NoteCpuWrite(PageAddress, TRACKER_BYTES_PER_PAGE);
        if (newly_hot != (rewrite == 2 ? 1u : 0u)) {
            return false;
        }
        MarkCpuDirty(manager);
    }

    manager.DecayHotPages();
    // The first post-decay read upload must clear the page again. The retained count is
    // halved (3 -> 1), so it takes two further refaults, not one, to become hot again.
    if (!IsOnePageUpload(Upload(manager, true)) ||
        manager.IsRegionModified<Type::CPU>(Page * TRACKER_BYTES_PER_PAGE,
                                            TRACKER_BYTES_PER_PAGE) ||
        manager.NoteCpuWrite(PageAddress, TRACKER_BYTES_PER_PAGE) != 0) {
        return false;
    }
    MarkCpuDirty(manager);
    if (!IsOnePageUpload(Upload(manager, true)) ||
        manager.NoteCpuWrite(PageAddress, TRACKER_BYTES_PER_PAGE) != 1) {
        return false;
    }
    return true;
}

bool TestWriteTargetUploadClearsHotAndReprotectHistory() {
    PageManager tracker{nullptr};
    RegionManager manager(&tracker, CpuBase);
    manager.SetHotPagesEnabled(true);
    MarkCpuDirty(manager);

    for (unsigned rewrite = 0; rewrite < 3; ++rewrite) {
        if (!IsOnePageUpload(Upload(manager, true))) {
            return false;
        }
        manager.NoteCpuWrite(PageAddress, TRACKER_BYTES_PER_PAGE);
        MarkCpuDirty(manager);
    }

    // An upload before the GPU writes this range invalidates hot-page treatment and the
    // pending refault marker. The dirty page is emitted, and a later read upload can clear it.
    if (!IsOnePageUpload(Upload(manager, false)) ||
        manager.IsRegionModified<Type::CPU>(Page * TRACKER_BYTES_PER_PAGE,
                                            TRACKER_BYTES_PER_PAGE) ||
        manager.NoteCpuWrite(PageAddress, TRACKER_BYTES_PER_PAGE) != 0) {
        return false;
    }
    MarkCpuDirty(manager);
    return IsOnePageUpload(Upload(manager, true)) &&
           !manager.IsRegionModified<Type::CPU>(Page * TRACKER_BYTES_PER_PAGE,
                                                TRACKER_BYTES_PER_PAGE);
}

} // namespace

int main() {
    if (!TestDisabledOptionKeepsOriginalClearBehavior()) return 1;
    if (!TestThresholdAndRepeatedReadUploads()) return 2;
    if (!TestDecayHalvesRefaultHistory()) return 3;
    if (!TestWriteTargetUploadClearsHotAndReprotectHistory()) return 4;
    return 0;
}
