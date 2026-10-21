// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Checks the render target input lookup: MatchTargetInputs must make the same decisions, leave the
// same entry state and call the generation getter at the same points as the original
// Rasterizer::TargetInputsMatch, and BytesEqual must agree with memcmp.

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <random>
#include <tuple>

#include "video_core/renderer_vulkan/bytes_equal.h"
#include "video_core/renderer_vulkan/target_inputs.h"

using Vulkan::BytesEqual;
using Vulkan::MatchTargetInputs;
using Vulkan::TargetInputsSize;

static_assert(TargetInputsSize<u64, u8, u16> == 11);
static_assert(TargetInputsSize<u32> == 4);

namespace {

// Same members as Rasterizer::CachedTarget.
struct Entry {
    bool valid{};
    u64 generation{};
    u64 touched_tick = ~u64{0};
    u32 size{};
    std::array<u8, 256> inputs{};
};

// Part shapes of the two lookups in PrepareRenderState, and some odd sizes.
struct alignas(4) ColorBuffer {
    u8 bytes[60];
};
struct alignas(4) Word {
    u32 raw;
};
struct alignas(4) DepthBuffer {
    u8 bytes[32];
};
struct alignas(8) Address {
    u64 raw;
};
struct Odd7 {
    u8 bytes[7];
};
struct Wide96 {
    u64 words[12];
};

// The original Rasterizer::TargetInputsMatch, with the generation getter as a callable.
template <typename GenerationFn, typename... Ts>
bool ReferenceMatch(Entry& cache, GenerationFn&& generation, const Ts&... parts) {
    std::array<u8, 256> inputs;
    u32 size = 0;
    static_assert((sizeof(Ts) + ...) <= inputs.size());
    ((std::memcpy(inputs.data() + size, &parts, sizeof(parts)), size += sizeof(parts)), ...);
    const bool match = cache.valid && cache.size == size && cache.generation == generation() &&
                       std::memcmp(cache.inputs.data(), inputs.data(), size) == 0;
    if (!match) {
        cache.valid = false;
        cache.size = size;
        std::memcpy(cache.inputs.data(), inputs.data(), size);
    }
    return match;
}

struct Counts {
    u64 matches = 0;
    u64 misses = 0;
    u64 generation_calls_new = 0;
    u64 generation_calls_ref = 0;
};

// A part whose bytes follow `seed`; now and then one byte differs, so both outcomes happen.
template <typename T>
T MakePart(std::mt19937& rng, u32 seed) {
    T value{};
    auto* bytes = reinterpret_cast<u8*>(&value);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        bytes[i] = static_cast<u8>(seed * 31u + i * 7u);
    }
    if (rng() % 12 == 0) {
        bytes[rng() % sizeof(T)] ^= 0x10;
    }
    return value;
}

template <typename... Ts>
void RunSequence(std::mt19937& rng, int steps, Counts& counts) {
    Entry actual{};
    Entry reference{};
    u64 generation = 1;
    u32 seed = 1;
    for (int step = 0; step < steps; ++step) {
        // The inputs change rarely, the generation and the entry's validity change sometimes.
        if (rng() % 9 == 0) {
            seed = rng() % 4;
        }
        if (rng() % 23 == 0) {
            ++generation;
        }
        if (rng() % 31 == 0) {
            actual.valid = false;
            reference.valid = false;
        }
        const auto parts = std::tuple<Ts...>{MakePart<Ts>(rng, seed)...};
        const bool got = std::apply(
            [&](const Ts&... p) {
                return MatchTargetInputs(
                    actual,
                    [&] {
                        ++counts.generation_calls_new;
                        return generation;
                    },
                    p...);
            },
            parts);
        const bool want = std::apply(
            [&](const Ts&... p) {
                return ReferenceMatch(
                    reference,
                    [&] {
                        ++counts.generation_calls_ref;
                        return generation;
                    },
                    p...);
            },
            parts);
        assert(got == want);
        assert(actual.valid == reference.valid);
        assert(actual.size == reference.size);
        assert(actual.generation == reference.generation);
        assert(actual.touched_tick == reference.touched_tick);
        assert(std::memcmp(actual.inputs.data(), reference.inputs.data(), actual.inputs.size()) ==
               0);
        if (got) {
            ++counts.matches;
        } else {
            ++counts.misses;
            // The caller does a full lookup and marks the entry valid with the generation.
            actual.valid = true;
            actual.generation = generation;
            reference.valid = true;
            reference.generation = generation;
        }
    }
}

void CheckBytesEqual(std::mt19937& rng) {
    std::array<u8, 320> a{};
    std::array<u8, 320> b{};
    for (int trial = 0; trial < 20000; ++trial) {
        const std::size_t size = rng() % 300;
        for (std::size_t i = 0; i < size; ++i) {
            a[i] = static_cast<u8>(rng() % 3);
            b[i] = a[i];
        }
        if (size != 0 && rng() % 2 == 0) {
            b[rng() % size] ^= static_cast<u8>(1u << (rng() % 8));
        }
        assert(BytesEqual(a.data(), b.data(), size) ==
               (std::memcmp(a.data(), b.data(), size) == 0));
    }
    // Sizes that are not a multiple of 8 or 4 at the start and end.
    for (std::size_t size = 0; size < 40; ++size) {
        for (std::size_t i = 0; i < size; ++i) {
            a[i] = b[i] = static_cast<u8>(i);
        }
        assert(BytesEqual(a.data(), b.data(), size));
        for (std::size_t i = 0; i < size; ++i) {
            b[i] ^= 0x80;
            assert(!BytesEqual(a.data(), b.data(), size));
            b[i] ^= 0x80;
        }
    }
}

} // namespace

int main() {
    std::mt19937 rng{20261020u};
    CheckBytesEqual(rng);

    Counts counts;
    for (int run = 0; run < 3; ++run) {
        // Color lookup: ColorBuffer + extent word.
        RunSequence<ColorBuffer, Word>(rng, 40000, counts);
        // Depth lookup: DepthBuffer, view, control, htile address, extent word.
        RunSequence<DepthBuffer, Word, Word, Address, Word>(rng, 40000, counts);
        // Odd sizes and a wide part.
        RunSequence<Odd7, Wide96>(rng, 40000, counts);
        RunSequence<u8, u16, u32>(rng, 40000, counts);
    }
    assert(counts.matches > 0);
    assert(counts.misses > 0);
    assert(counts.generation_calls_new == counts.generation_calls_ref);

    std::printf("test_target_inputs: %llu matches, %llu misses, %llu generation reads, all as the "
                "reference\n",
                static_cast<unsigned long long>(counts.matches),
                static_cast<unsigned long long>(counts.misses),
                static_cast<unsigned long long>(counts.generation_calls_new));
    return 0;
}
