// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Vulkan {

/// Whether a stage binds any texture or sampler. BindTextures only works on those two lists, so
/// for a stage without them the call has nothing to do (and it has a large stack frame).
template <typename Stage>
[[nodiscard]] inline bool HasTextureBindings(const Stage& stage) noexcept {
    return !stage.images.empty() || !stage.samplers.empty();
}

} // namespace Vulkan
