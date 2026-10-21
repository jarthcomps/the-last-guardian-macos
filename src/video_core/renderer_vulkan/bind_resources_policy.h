// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

namespace Vulkan {

/// Whether a stage binds any texture or sampler. BindTextures only works on those two lists, so
/// for a stage without them the call has nothing to do (and it has a large stack frame).
template <typename Stage>
[[nodiscard]] inline bool HasTextureBindings(const Stage& stage) noexcept {
    return !stage.images.empty() || !stage.samplers.empty();
}

/// The viewport values of the push constants (Rasterizer's MakeUserData). An offset is 0 and a
/// scale is 1 while its viewport_control enable bit is clear. The selects are branches on purpose:
/// the compiler turns selects like these into SSE4.1 blends, which translated (Rosetta) code runs
/// about 10 ns slower per call in a micro-benchmark.
template <typename Regs, typename PushData>
inline void FillViewportPush(const Regs& regs, PushData& push_data) noexcept {
    const auto& control = regs.viewport_control;
    const auto& viewport = regs.viewports[0];
    if (control.xoffset_enable) {
        push_data.xoffset = viewport.xoffset;
    } else {
        push_data.xoffset = 0.f;
    }
    if (control.xscale_enable) {
        push_data.xscale = viewport.xscale;
    } else {
        push_data.xscale = 1.f;
    }
    if (control.yoffset_enable) {
        push_data.yoffset = viewport.yoffset;
    } else {
        push_data.yoffset = 0.f;
    }
    if (control.yscale_enable) {
        push_data.yscale = viewport.yscale;
    } else {
        push_data.yscale = 1.f;
    }
}

/// Descriptor writes the stages of a pipeline take: one per buffer, one per image descriptor and
/// one per sampler of each stage. A null stage takes none.
template <typename Stages>
[[nodiscard]] inline std::size_t DescriptorWriteTotal(const Stages& stages) noexcept {
    std::size_t total = 0;
    for (const auto* stage : stages) {
        if (stage) {
            total += stage->buffers.size() + stage->images.size() + stage->samplers.size();
        }
    }
    return total;
}

} // namespace Vulkan
