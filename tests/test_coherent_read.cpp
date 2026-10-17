// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "video_core/coherent_read.h"

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); std::abort(); \
} } while (false)

int main() {
    std::array<unsigned char, 8> guest{1, 2, 3, 4, 5, 6, 7, 8};
    auto backing = guest;
    const auto address = reinterpret_cast<std::uintptr_t>(guest.data());
    int checks = 0;
    int lookups = 0;
    bool current = true;
    bool shared = true;
    auto is_current = [&](std::uintptr_t a, std::uint64_t size) {
        ++checks;
        CHECK(a == address && size == guest.size());
        return current;
    };
    auto find_backing = [&](std::uintptr_t a, std::uint64_t size) -> const void* {
        ++lookups;
        CHECK(a == address && size == guest.size());
        return shared ? backing.data() : nullptr;
    };
    using VideoCore::CoherentReadPointer;
    CHECK(CoherentReadPointer(address, 8, false, is_current, find_backing) == guest.data());
    CHECK(checks == 0 && lookups == 0);
    CHECK(CoherentReadPointer(address, 0, true, is_current, find_backing) == guest.data());
    CHECK(checks == 0 && lookups == 0);
    current = false; // GPU-dirty bytes or any pending asynchronous copy must use guest reads.
    CHECK(CoherentReadPointer(address, 8, true, is_current, find_backing) == guest.data());
    CHECK(checks == 1 && lookups == 0);
    current = true;
    shared = false; // Executable, unmapped or noncontiguous physical backing is rejected.
    CHECK(CoherentReadPointer(address, 8, true, is_current, find_backing) == guest.data());
    CHECK(checks == 2 && lookups == 1);
    shared = true;
    const void* selected = CoherentReadPointer(address, 8, true, is_current, find_backing);
    CHECK(selected == backing.data());
    CHECK(checks == 3 && lookups == 2);
    CHECK(std::memcmp(selected, guest.data(), guest.size()) == 0);
    return 0;
}
