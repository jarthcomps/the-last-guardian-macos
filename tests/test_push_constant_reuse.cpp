// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstdint>
#include "video_core/renderer_vulkan/push_constant_reuse.h"

#define CHECK(x) do { if (!(x)) return __LINE__; } while (false)

int main() {
    Vulkan::PushConstantReuse<std::uintptr_t> memo;
    std::array<std::uint32_t, 65> data{};
    CHECK(!memo.CanSkip(1, 1, 0, 16, data.data()));
    CHECK(memo.CanSkip(1, 1, 0, 16, data.data()));
    // Caller storage is copied. A changed byte at either end needs another command.
    data[0] = 1;
    CHECK(!memo.CanSkip(1, 1, 0, 16, data.data()));
    CHECK(memo.CanSkip(1, 1, 0, 16, data.data()));
    data[3] = 2;
    CHECK(!memo.CanSkip(1, 1, 0, 16, data.data()));
    // Layouts, stages, byte offsets, lengths and intervening partial writes are independent.
    CHECK(!memo.CanSkip(2, 1, 0, 16, data.data()));
    CHECK(!memo.CanSkip(2, 2, 0, 16, data.data()));
    CHECK(!memo.CanSkip(2, 2, 4, 16, data.data()));
    CHECK(!memo.CanSkip(2, 2, 4, 12, data.data()));
    CHECK(!memo.CanSkip(2, 2, 0, 16, data.data()));
    CHECK(memo.CanSkip(2, 2, 0, 16, data.data()));
    memo.Reset();
    CHECK(!memo.CanSkip(2, 2, 0, 16, data.data()));
    // Unsupported calls invalidate the remembered state instead of skipping over a write.
    for (auto [layout, stages, offset, size] : std::array<std::array<std::uint32_t, 4>, 7>{{
             {0, 2, 0, 16}, {2, 0, 0, 16}, {2, 2, 1, 16}, {2, 2, 0, 15},
             {2, 2, 0, 0}, {2, 2, 0, 260}, {2, 2, UINT32_MAX - 3, 16}}}) {
        CHECK(!memo.CanSkip(layout, stages, offset, size, data.data()));
        CHECK(!memo.CanSkip(2, 2, 0, 16, data.data()));
        CHECK(memo.CanSkip(2, 2, 0, 16, data.data()));
    }
    CHECK(!memo.CanSkip(2, 2, 0, 16, nullptr));
    CHECK(!memo.CanSkip(2, 2, 0, 16, data.data()));
    Vulkan::PushConstantReuse<std::uintptr_t> other;
    CHECK(!other.CanSkip(2, 2, 0, 16, data.data()));
    CHECK(memo.CanSkip(2, 2, 0, 16, data.data()));
    CHECK(!memo.CanSkip(1, 1, 0, 256, data.data()));
    CHECK(memo.CanSkip(1, 1, 0, 256, data.data()));
    return 0;
}
