// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cassert>
#include <cstddef>
#include <cstring>
#include "video_core/renderer_vulkan/vk_pipeline_memo.h"

namespace {

struct Pipeline {
    int id;
};

// Stands in for GraphicsPipelineKey: a large trivially copyable key compared bytewise.
struct BigKey {
    unsigned char bytes[330]{};

    bool operator==(const BigKey& other) const noexcept {
        return std::memcmp(this, &other, sizeof(*this)) == 0;
    }
};

// Stands in for ComputePipelineKey and the program hashes.
struct SmallKey {
    std::size_t value;

    friend bool operator==(const SmallKey& lhs, const SmallKey& rhs) {
        return lhs.value == rhs.value;
    }
};

void TestSmallKeys() {
    Vulkan::PipelineLookupMemo<SmallKey, Pipeline> memo;
    Pipeline first{1};
    Pipeline second{2};

    assert(memo.Find(SmallKey{7}) == nullptr);
    memo.Store(SmallKey{7}, &first);
    assert(memo.Find(SmallKey{7}) == &first);
    assert(memo.Find(SmallKey{8}) == nullptr);

    // Storing another key replaces the remembered one.
    memo.Store(SmallKey{8}, &second);
    assert(memo.Find(SmallKey{7}) == nullptr);
    assert(memo.Find(SmallKey{8}) == &second);

    memo.Invalidate();
    assert(memo.Find(SmallKey{8}) == nullptr);

    // A null value remembers nothing, not even for the same key.
    memo.Store(SmallKey{9}, nullptr);
    assert(memo.Find(SmallKey{9}) == nullptr);
}

void TestBigKeys() {
    Vulkan::PipelineLookupMemo<BigKey, const Pipeline> memo;
    Pipeline pipeline{3};
    BigKey key{};
    key.bytes[200] = 5;

    assert(memo.Find(key) == nullptr);
    memo.Store(key, &pipeline);

    // An equal key, built separately, finds the remembered pipeline.
    BigKey copy{};
    copy.bytes[200] = 5;
    assert(memo.Find(copy) == &pipeline);

    // A key that differs in the last byte is a different key.
    copy.bytes[329] = 1;
    assert(memo.Find(copy) == nullptr);

    // The memo keeps its own copy of the key: later changes to the original do not leak in.
    key.bytes[0] = 9;
    assert(memo.Find(key) == nullptr);
    key.bytes[0] = 0;
    assert(memo.Find(key) == &pipeline);
}

} // namespace

int main() {
    TestSmallKeys();
    TestBigKeys();
    return 0;
}
