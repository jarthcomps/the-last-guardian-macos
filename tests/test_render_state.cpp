// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstring>
#include <new>
#include "video_core/renderer_vulkan/vk_render_state.h"
int main() {
    alignas(Vulkan::RenderState) std::array<unsigned char, sizeof(Vulkan::RenderState)> bytes;
    bytes.fill(0x43);
    auto* state = new (bytes.data()) Vulkan::RenderState;
    for (auto byte : bytes) if (byte != 0) return 1;
    auto& attachment = state->depth_stencil_attachment;
    attachment.has_depth = true;
    attachment.depth_clear = true;
    if (attachment.has_stencil || attachment.stencil_clear) return 2;
    state->~RenderState();
    bytes.fill(0x43);
    state = new (bytes.data()) Vulkan::RenderState;
    auto& stencil = state->depth_stencil_attachment;
    stencil.has_stencil = true;
    stencil.stencil_clear = true;
    if (stencil.has_depth || stencil.depth_clear) return 3;
    const Vulkan::RenderState copied = *state;
    if (!(copied == *state)) return 4;
    state->~RenderState();
    Vulkan::RenderState first, second;
    if (!(first == second)) return 5;
    second.num_layers = 1;
    if (first == second) return 6;
}
