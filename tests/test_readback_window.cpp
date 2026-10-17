// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <cstdlib>
#include <limits>
#include "video_core/buffer_cache/readback_window.h"

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); std::abort(); \
} } while (false)

int main() {
    using namespace VideoCore;
    CHECK(ReadbackWindowSize("") == 512 * 1024);
    for (auto invalid : {"0", "1", "16", "63", "1024", "-64", "64garbage"}) {
        CHECK(ReadbackWindowSize(invalid) == 512 * 1024);
    }
    CHECK(ReadbackWindowSize("64") == 64 * 1024);
    CHECK(ReadbackWindowSize("128") == 128 * 1024);
    CHECK(ReadbackWindowSize("256") == 256 * 1024);
    for (auto value : {"64", "128", "256", "512"}) {
        const auto window = ReadbackWindowSize(value);
        for (std::uint64_t address = 0x100003; address < 0x300000; address += 0x3ff1) {
            const auto [start, end] = CalculateReadbackWindow(
                0x100003, 0x400000, address, 0x18000, window);
            CHECK(start >= 0x100003 && end <= 0x400000);
            CHECK(start <= address && end >= address + 0x18000);
        }
        const auto [start, end] = CalculateReadbackWindow(0x1003, 0x3010, 0x3000, 16, window);
        CHECK(start == 0x1003 && end == 0x3010);
    }
    const auto max = std::numeric_limits<std::uint64_t>::max();
    CHECK(ReadbackWindowEnd(max - 8, 16) == max);
    CHECK(ReadbackWindowEnd(0x1000, 0x2000) == 0x3000);
    const auto [start, end] = CalculateReadbackWindow(max - 0xffff, max, max - 8, 8, 0x10000);
    CHECK(start == max - 0xffff && end == max);
    return 0;
}
