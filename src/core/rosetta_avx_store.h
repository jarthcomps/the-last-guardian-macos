// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <xbyak/xbyak.h>

namespace Core {

template <typename AddressGenerator>
void EmitRosettaAvxStore(Xbyak::CodeGenerator& c, int source_index,
                        AddressGenerator&& generate_address) {
    using namespace Xbyak::util;
    const Xbyak::Ymm source(source_index);
    c.lea(rsp, ptr[rsp - 128]);
    c.push(r11);
    generate_address(c, r11, 136);
    c.movups(ptr[r11], source.cvt128());
    c.vextractf128(ptr[r11 + 16], source, 1);
    c.pop(r11);
    c.lea(rsp, ptr[rsp + 128]);
}

} // namespace Core
