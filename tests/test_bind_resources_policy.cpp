// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
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
    std::vector<int> buffers;
};

// Same field layout as AmdGpu::ViewportControl and AmdGpu::ViewportBounds.
struct FakeViewportControl {
    std::uint32_t xscale_enable : 1;
    std::uint32_t xoffset_enable : 1;
    std::uint32_t yscale_enable : 1;
    std::uint32_t yoffset_enable : 1;
    std::uint32_t zscale_enable : 1;
    std::uint32_t zoffset_enable : 1;
    std::uint32_t : 2;
    std::uint32_t xy_transformed : 1;
    std::uint32_t z_transformed : 1;
    std::uint32_t w_transformed : 1;
    std::uint32_t perfcounter_ref : 1;
};

struct FakeViewport {
    float xscale;
    float xoffset;
    float yscale;
    float yoffset;
    float zscale;
    float zoffset;
};

struct FakeRegs {
    FakeViewportControl viewport_control{};
    std::array<FakeViewport, 16> viewports{};
};

struct FakePushData {
    float xoffset;
    float yoffset;
    float xscale;
    float yscale;
};

// The selects that MakeUserData used before FillViewportPush replaced them.
FakePushData ReferenceViewportPush(const FakeRegs& regs) {
    FakePushData out{};
    out.xoffset = regs.viewport_control.xoffset_enable ? regs.viewports[0].xoffset : 0.f;
    out.xscale = regs.viewport_control.xscale_enable ? regs.viewports[0].xscale : 1.f;
    out.yoffset = regs.viewport_control.yoffset_enable ? regs.viewports[0].yoffset : 0.f;
    out.yscale = regs.viewport_control.yscale_enable ? regs.viewports[0].yscale : 1.f;
    return out;
}

bool SameBits(float a, float b) {
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

} // namespace

int main() {
    // BindTextures is skipped only for a stage with neither images nor samplers.
    assert(!Vulkan::HasTextureBindings(FakeStage{}));
    assert(Vulkan::HasTextureBindings(FakeStage{{1}, {}, {}}));
    assert(Vulkan::HasTextureBindings(FakeStage{{}, {2}, {}}));
    assert(Vulkan::HasTextureBindings(FakeStage{{1, 2}, {3}, {}}));

    // Each buffer, image descriptor and sampler of a non-null stage takes one descriptor write.
    const FakeStage vertex{{1}, {}, {1, 2}};
    const FakeStage pixel{{}, {5, 6}, {}};
    const std::array<const FakeStage*, 3> stages{&vertex, nullptr, &pixel};
    assert(Vulkan::DescriptorWriteTotal(stages) == 5);
    assert(Vulkan::DescriptorWriteTotal(std::array<const FakeStage*, 1>{nullptr}) == 0);
    assert(Vulkan::DescriptorWriteTotal(std::array<const FakeStage*, 0>{}) == 0);

    // FillViewportPush gives the same bits as the selects it replaces, for every combination of
    // enable bits and for values a select must carry unchanged (-0, NaN, infinity, denormal). It
    // also overwrites values left from an earlier call.
    const std::array<float, 5> samples{
        -0.0f, std::bit_cast<float>(0x7fc12345u), std::numeric_limits<float>::infinity(),
        1e-40f, 123.5f};
    for (std::uint32_t mask = 0; mask < 16; ++mask) {
        for (const float sample : samples) {
            FakeRegs regs{};
            regs.viewport_control.xscale_enable = mask & 1u;
            regs.viewport_control.xoffset_enable = (mask >> 1) & 1u;
            regs.viewport_control.yscale_enable = (mask >> 2) & 1u;
            regs.viewport_control.yoffset_enable = (mask >> 3) & 1u;
            regs.viewports[0] = {sample, sample + 2.0f, sample - 3.0f, -sample, 7.0f, 8.0f};
            FakePushData actual{99.f, 98.f, 97.f, 96.f};
            Vulkan::FillViewportPush(regs, actual);
            const FakePushData expected = ReferenceViewportPush(regs);
            assert(SameBits(actual.xoffset, expected.xoffset));
            assert(SameBits(actual.yoffset, expected.yoffset));
            assert(SameBits(actual.xscale, expected.xscale));
            assert(SameBits(actual.yscale, expected.yscale));
        }
    }

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
