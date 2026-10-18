// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <cassert>
#include <random>
#include <vector>
#include "video_core/buffer_cache/region_manager.h"
namespace {
struct Event { bool track; VideoCore::RegionBits mask; bool operator==(const Event&) const = default; };
std::vector<Event> events;
}
namespace VideoCore {
struct PageManager::Impl {};
PageManager::PageManager(Vulkan::Rasterizer*) : impl{std::make_unique<Impl>()} {}
PageManager::~PageManager() = default;
template <> void PageManager::UpdatePageWatchersForRegion<true, false>(VAddr, RegionBits& mask) const { events.push_back({true, mask}); }
template <> void PageManager::UpdatePageWatchersForRegion<false, false>(VAddr, RegionBits& mask) const { events.push_back({false, mask}); }
}
int main() {
    using namespace VideoCore;
    std::mt19937 random(41);
    RegionBits a, b;
    for (unsigned i = 0; i < NUM_PAGES_PER_REGION; ++i) {
        if (random() & 1) a.Set(i);
        if (random() & 1) b.Set(i);
    }
    for (size_t start = 0; start <= NUM_PAGES_PER_REGION; ++start) {
        for (size_t length : {0u, 1u, 2u, 63u, 64u, 65u, 1024u}) {
            const auto end = std::min(start + length, size_t(NUM_PAGES_PER_REGION));
            auto updated = a;
            const RegionBits expected(a ^ b, start, end);
            assert(updated.AssignRange(b, start, end) == expected);
            assert(updated == (a ^ expected));
            assert(updated.AssignRange(b, start, end).None());
        }
    }
    assert(a.AssignRange(b, 5, 4).None());
    assert(a.AssignRange(b, 0, NUM_PAGES_PER_REGION + 1).None());
    PageManager tracker{nullptr};
    constexpr VAddr base = 0x10000000;
    RegionManager baseline{&tracker, base}, candidate{&tracker, base};
    candidate.SetRangeProtectionEnabled(true);
    baseline.SetHotPagesEnabled(true); candidate.SetHotPagesEnabled(true);
    baseline.GetRegionBits<Type::GPU>().Set(64);
    candidate.GetRegionBits<Type::GPU>().Set(64);
    auto mutate = [&](RegionManager& m, unsigned op, size_t first, size_t count) {
        const auto address = base + first * TRACKER_BYTES_PER_PAGE + 1;
        const auto bytes = count * TRACKER_BYTES_PER_PAGE - 2;
        switch (op) {
        case 0: m.NoteCpuWrite(address, bytes);
                m.ChangeRegionState<Type::CPU, true>(address, bytes); break;
        case 1: m.ChangeRegionState<Type::CPU, false>(address, bytes); break;
        case 2: m.ForEachModifiedRange<Type::CPU, true>(address, bytes, [](VAddr, u64){}, true); break;
        case 3: m.ForEachModifiedRange<Type::CPU, true>(address, bytes, [](VAddr, u64){}, false); break;
        case 4: m.DecayHotPages(); break;
        }
    };
    for (unsigned i = 0; i < 6000; ++i) {
        const size_t first = random() % NUM_PAGES_PER_REGION;
        const size_t count = std::min<size_t>(1 + random() % 130, NUM_PAGES_PER_REGION - first);
        const unsigned op = random() % 5;
        events.clear(); mutate(baseline, op, first, count); const auto expected = events;
        events.clear(); mutate(candidate, op, first, count); assert(events == expected);
        assert(baseline.GetRegionBits<Type::CPU>() == candidate.GetRegionBits<Type::CPU>());
        assert(baseline.GetRegionBits<Type::GPU>() == candidate.GetRegionBits<Type::GPU>());
    }
    // Repeated invalidation of an already writable page must emit no new protection request.
    mutate(candidate, 0, 63, 2); events.clear(); mutate(candidate, 0, 63, 2);
    assert(events.empty());
    return 0;
}
