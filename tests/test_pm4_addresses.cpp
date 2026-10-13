// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdint>
#include <span>
#include "video_core/amdgpu/pm4_cmds.h"

int main() {
    constexpr u64 full_address = 0x12345678abcdef00ULL;
    AmdGpu::PM4CmdWriteData write{.header = {AmdGpu::PM4ItOpcode::WriteData, 3}};
    write.Address(full_address);
    if (write.Address<VAddr>() != full_address ||
        write.Address<uintptr_t>() != full_address ||
        reinterpret_cast<uintptr_t>(write.Address<u32*>()) != full_address) {
        return 1;
    }

    constexpr u64 fence_address = 0xabcd12345678ULL;
    AmdGpu::PM4CmdEventWriteEos fence{.header = {AmdGpu::PM4ItOpcode::EventWriteEos, 3}};
    fence.address_lo = static_cast<u32>(fence_address);
    fence.cmd_info = static_cast<u32>(fence_address >> 32) | (2u << 29);
    if (fence.Address<VAddr>() != fence_address ||
        fence.Address<uintptr_t>() != fence_address ||
        reinterpret_cast<uintptr_t>(fence.Address()) != fence_address) {
        return 2;
    }
    return 0;
}
