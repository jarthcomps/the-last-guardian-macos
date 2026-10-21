// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// ImageResource::NumBindingsOf, given the sharp that GetSharp returned, must give what
// NumBindings(info) gives (which fetches the sharp itself), and both must follow the mip count
// rule the specialization has always used.

#include <cassert>
#include <random>
#include <vector>
#include "shader_recompiler/resource.h"

namespace {

// Only the flattened user data is needed: GetSharp reads nothing else from the info.
struct FakeInfo {
    std::vector<u32> flattened_ud_buf;
};

// The rule as it was written in ImageResource::NumBindings before the sharp could be passed in.
u32 ReferenceNumBindings(const Shader::ImageResource& desc, const AmdGpu::Image& tsharp) {
    if (desc.mip_fallback_mode == Shader::MipStorageFallbackMode::DynamicIndex) {
        return tsharp.last_level - tsharp.base_level + 1;
    }
    return 1;
}

} // namespace

int main() {
    std::mt19937 rng(7);
    const Shader::MipStorageFallbackMode modes[] = {
        Shader::MipStorageFallbackMode::None,
        Shader::MipStorageFallbackMode::DynamicIndex,
        Shader::MipStorageFallbackMode::ConstantIndex,
    };
    for (int i = 0; i < 20000; ++i) {
        FakeInfo info;
        info.flattened_ud_buf.resize(Shader::SharpFetch<AmdGpu::Image>::N);
        for (auto& dw : info.flattened_ud_buf) {
            dw = rng();
        }

        Shader::ImageResource desc{};
        for (u32 k = 0; k < Shader::SharpFetch<AmdGpu::Image>::N; ++k) {
            desc.sharp_fetch.offsets[k] = static_cast<Shader::SharpLocation>(k);
        }
        desc.sharp_fetch.load_mask = 0xFF;
        desc.is_r128 = (rng() % 4) == 0;
        desc.mip_fallback_mode = modes[rng() % 3];

        // The fetched sharp, as the specialization passes it on.
        const AmdGpu::Image tsharp = desc.GetSharp(info);
        assert(desc.NumBindingsOf(tsharp) == ReferenceNumBindings(desc, tsharp));
        assert(desc.NumBindings(info) == desc.NumBindingsOf(tsharp));
        assert(desc.NumBindings(info) == ReferenceNumBindings(desc, desc.GetSharp(info)));

        // Every level count, not only the ones the random words produce.
        AmdGpu::Image synthetic{};
        synthetic.base_level = rng() % 16;
        synthetic.last_level = rng() % 16;
        assert(desc.NumBindingsOf(synthetic) == ReferenceNumBindings(desc, synthetic));
    }
    return 0;
}
