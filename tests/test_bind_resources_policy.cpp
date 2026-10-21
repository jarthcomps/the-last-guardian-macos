// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "video_core/renderer_vulkan/barrier_stats.h"
#include "video_core/renderer_vulkan/bind_resources_policy.h"
#include "video_core/renderer_vulkan/compute_dispatch_capture_plan.h"

namespace {

struct FakeStage {
    std::vector<int> images;
    std::vector<int> samplers;
};

} // namespace

int main() {
    // BindTextures is skipped only for a stage with neither images nor samplers.
    assert(!Vulkan::HasTextureBindings(FakeStage{}));
    assert(Vulkan::HasTextureBindings(FakeStage{{1}, {}}));
    assert(Vulkan::HasTextureBindings(FakeStage{{}, {2}}));
    assert(Vulkan::HasTextureBindings(FakeStage{{1, 2}, {3}}));

    // A barrier scope matters only to the statistics or to SHADPS4_DROP_BARRIERS.
    static_assert(!Vulkan::BarrierStats::ScopeNeeded(false, 0));
    static_assert(Vulkan::BarrierStats::ScopeNeeded(true, 0));
    static_assert(Vulkan::BarrierStats::ScopeNeeded(false, 1u << 1));
    static_assert(Vulkan::BarrierStats::ScopeNeeded(true, 0x40));

    // Capture needs a non-empty directory that holds the arm marker, and the marker is looked at
    // on every call: arming and disarming take effect without reading the variable again.
    using namespace Vulkan::ComputeCapture;
    assert(!CaptureEnabledIn(std::string_view{}));
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("shadps4-bind-policy-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const std::string path = directory.string();
    assert(!CaptureEnabledIn(path));
    std::ofstream(directory / CaptureArmMarkerName) << "ready\n";
    assert(CaptureEnabledIn(path));
    std::filesystem::remove(directory / CaptureArmMarkerName);
    assert(!CaptureEnabledIn(path));
    std::filesystem::remove_all(directory);

    return 0;
}
