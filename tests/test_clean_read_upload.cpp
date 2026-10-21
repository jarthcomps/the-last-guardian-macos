// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// A CPU read upload of a range with no CPU modified page must change nothing: it reports no
// range to copy, asks the page tracker for no protection change, and leaves every bit of the
// region as it was. BufferCache::ObtainBuffer skips such reads, so this test checks that the
// skipped calls and the real ones leave identical state behind for every later operation.

#include <algorithm>
#include <cassert>
#include <random>
#include <utility>
#include <vector>
#include "video_core/buffer_cache/region_manager.h"

namespace {

struct Event {
    bool track;
    VideoCore::RegionBits mask;
    bool operator==(const Event&) const = default;
};

std::vector<Event> events;

using Ranges = std::vector<std::pair<VAddr, u64>>;

} // namespace

namespace VideoCore {
struct PageManager::Impl {};

PageManager::PageManager(Vulkan::Rasterizer*) : impl{std::make_unique<Impl>()} {}
PageManager::~PageManager() = default;

template <>
void PageManager::UpdatePageWatchersForRegion<true, false>(VAddr, RegionBits& mask) const {
    events.push_back({true, mask});
}
template <>
void PageManager::UpdatePageWatchersForRegion<false, false>(VAddr, RegionBits& mask) const {
    events.push_back({false, mask});
}
} // namespace VideoCore

namespace {

using namespace VideoCore;

constexpr VAddr Base = 0x10000000;
constexpr u64 RegionBytes = TRACKER_HIGHER_PAGE_SIZE;

Ranges Upload(RegionManager& manager, u64 offset, u64 bytes, bool keep_hot) {
    Ranges ranges;
    manager.ForEachModifiedRange<Type::CPU, true>(
        Base + offset, static_cast<s64>(bytes),
        [&](VAddr address, u64 size) { ranges.emplace_back(address, size); }, keep_hot);
    return ranges;
}

// A fresh region is entirely CPU modified: the first read uploads all of it, and after that
// reads of it find nothing to copy and nothing to change.
bool TestFreshRegionReadsAreNoOpsAfterFirstUpload() {
    PageManager tracker{nullptr};
    RegionManager manager(&tracker, Base);
    events.clear();
    const auto first = Upload(manager, 0, RegionBytes, true);
    if (first.size() != 1 || first[0].first != Base || first[0].second != RegionBytes ||
        events.size() != 1) {
        return false;
    }
    events.clear();
    for (unsigned read = 0; read < 3; ++read) {
        const bool clean = !manager.IsRegionModified<Type::CPU>(0, RegionBytes);
        if (!clean || !Upload(manager, 0, RegionBytes, true).empty() || !events.empty()) {
            return false;
        }
    }
    return true;
}

// Random CPU writes, write-target uploads, read uploads, decays and unmarks. Every read of a
// clean range is skipped on one manager and run on the other. Both must stay identical: same
// tracker events, same copies, same CPU bits, same results of the write-after-upload probe.
bool RunSkipEquivalence(bool hot_pages, bool range_protection, unsigned seed) {
    PageManager tracker{nullptr};
    RegionManager real(&tracker, Base), skipped(&tracker, Base);
    for (RegionManager* manager : {&real, &skipped}) {
        manager->SetHotPagesEnabled(hot_pages);
        manager->SetRangeProtectionEnabled(range_protection);
    }

    std::mt19937 random(seed);
    unsigned clean_reads = 0;
    for (unsigned step = 0; step < 4000; ++step) {
        const u64 offset = random() % RegionBytes;
        const u64 bytes = 1 + random() % std::min<u64>(RegionBytes - offset, 3 * 1024 * 1024);
        const unsigned op = random() % 5;

        if (op == 2) {
            // A read upload. Clean ranges are the case under test.
            const bool clean = !real.IsRegionModified<Type::CPU>(offset, bytes);
            events.clear();
            const Ranges real_ranges = Upload(real, offset, bytes, true);
            const auto real_events = events;
            if (clean) {
                if (!real_ranges.empty() || !real_events.empty()) {
                    return false;
                }
                ++clean_reads;
            } else {
                events.clear();
                const Ranges skipped_ranges = Upload(skipped, offset, bytes, true);
                if (real_ranges != skipped_ranges || real_events != events) {
                    return false;
                }
            }
        } else {
            events.clear();
            Ranges real_ranges;
            u32 real_probe = 0;
            switch (op) {
            case 0:
                real_probe = real.NoteCpuWrite(Base + offset, bytes);
                real.ChangeRegionState<Type::CPU, true>(Base + offset, bytes);
                break;
            case 1:
                real_ranges = Upload(real, offset, bytes, false);
                break;
            case 3:
                real.DecayHotPages();
                break;
            case 4:
                real.ChangeRegionState<Type::CPU, false>(Base + offset, bytes);
                break;
            }
            const auto real_events = events;
            events.clear();
            Ranges skipped_ranges;
            u32 skipped_probe = 0;
            switch (op) {
            case 0:
                skipped_probe = skipped.NoteCpuWrite(Base + offset, bytes);
                skipped.ChangeRegionState<Type::CPU, true>(Base + offset, bytes);
                break;
            case 1:
                skipped_ranges = Upload(skipped, offset, bytes, false);
                break;
            case 3:
                skipped.DecayHotPages();
                break;
            case 4:
                skipped.ChangeRegionState<Type::CPU, false>(Base + offset, bytes);
                break;
            }
            if (real_events != events || real_ranges != skipped_ranges ||
                real_probe != skipped_probe) {
                return false;
            }
        }

        if (real.GetRegionBits<Type::CPU>() != skipped.GetRegionBits<Type::CPU>()) {
            return false;
        }
    }
    return clean_reads > 0;
}

} // namespace

int main() {
    const bool fresh_region_ok = TestFreshRegionReadsAreNoOpsAfterFirstUpload();
    assert(fresh_region_ok);
    unsigned config = 0;
    for (const bool hot_pages : {false, true}) {
        for (const bool range_protection : {false, true}) {
            for (unsigned seed = 1; seed <= 4; ++seed) {
                const bool equivalent =
                    RunSkipEquivalence(hot_pages, range_protection, 100 * config + seed);
                assert(equivalent);
            }
            ++config;
        }
    }
    return 0;
}
