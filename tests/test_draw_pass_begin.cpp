// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cassert>

#include "video_core/renderer_vulkan/fence_deferral.h"

using Vulkan::ShouldBeginDrawPass;

static_assert(ShouldBeginDrawPass(true, 7, 7), "a new pass always begins");
static_assert(ShouldBeginDrawPass(true, 7, 8), "a new pass always begins");
static_assert(ShouldBeginDrawPass(false, 7, 8), "the open pass changed in between");
static_assert(!ShouldBeginDrawPass(false, 7, 7), "the same pass is still open");

namespace {

// Mirrors the pass bookkeeping of Vulkan::Scheduler (BeginRendering, EndRendering and the epoch)
// with an integer render state, so the draw protocol can be checked without Vulkan.
struct PassModel {
    bool is_rendering = false;
    int state = -1;
    u64 epoch = 0;

    bool IsRenderingState(int s) const {
        return is_rendering && state == s;
    }

    void EndRendering() {
        if (!is_rendering) {
            return;
        }
        ++epoch;
        is_rendering = false;
    }

    void BeginRendering(int s) {
        if (is_rendering && state == s) {
            return;
        }
        ++epoch;
        EndRendering();
        is_rendering = true;
        state = s;
    }
};

struct Rng {
    u64 s;
    unsigned Next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return static_cast<unsigned>(s >> 11);
    }
};

} // namespace

int main() {
    // `reference` begins the pass on every draw, as Rasterizer::Draw did before. `optimized`
    // follows the rule the draw uses. Both see the same work between the check and the call:
    // nothing, a flush or buffer bind that ends the pass, or one that begins another pass.
    PassModel reference;
    PassModel optimized;
    Rng rng{0x9E3779B97F4A7C15ull};
    unsigned skipped = 0;
    for (unsigned draw = 0; draw < 200000; ++draw) {
        const int s = static_cast<int>(rng.Next() % 3);

        const bool new_pass_reference = !reference.IsRenderingState(s);
        const bool new_pass_optimized = !optimized.IsRenderingState(s);
        assert(new_pass_reference == new_pass_optimized);
        const u64 epoch_at_check = optimized.epoch;

        const unsigned work = rng.Next() % 3;
        for (unsigned i = 0; i < work; ++i) {
            const unsigned op = rng.Next() % 4;
            if (op == 1) {
                reference.EndRendering();
                optimized.EndRendering();
            } else if (op == 2) {
                const int other = static_cast<int>(rng.Next() % 3);
                reference.BeginRendering(other);
                optimized.BeginRendering(other);
            }
        }

        reference.BeginRendering(s);
        if (ShouldBeginDrawPass(new_pass_optimized, epoch_at_check, optimized.epoch)) {
            optimized.BeginRendering(s);
        } else {
            // Skipped only when the call would have returned at once.
            ++skipped;
            assert(optimized.is_rendering && optimized.state == s);
        }

        assert(reference.is_rendering && reference.state == s);
        assert(optimized.is_rendering == reference.is_rendering);
        assert(optimized.state == reference.state);
    }
    // The rule must actually skip calls in this run, or the check above proves little.
    assert(skipped > 10000);
    return 0;
}
