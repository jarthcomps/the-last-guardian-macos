// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/env_switch.h"

int main() {
    using Common::EnvOverride;
    if (EnvOverride(nullptr, false) || !EnvOverride(nullptr, true)) return 1;
    if (!EnvOverride("1", false) || !EnvOverride("1", true)) return 2;
    if (EnvOverride("0", true) || EnvOverride("0", false)) return 3;
    if (EnvOverride("", false) || !EnvOverride("", true)) return 4;
    if (EnvOverride("yes", false) || !EnvOverride("10", true)) return 5;
    static_assert(EnvOverride("1", false));
    using Common::EnvOverrideNumber;
    if (EnvOverrideNumber(nullptr, 8) != 8 || EnvOverrideNumber("", 8) != 8) return 6;
    if (EnvOverrideNumber("128", 8) != 128 || EnvOverrideNumber("0", 8) != 0) return 7;
    if (EnvOverrideNumber("12a", 8) != 8 || EnvOverrideNumber("-3", 8) != 8) return 8;
    if (EnvOverrideNumber("99999999999", 8) != 8) return 9;
    static_assert(EnvOverrideNumber("3", 0) == 3);
    return 0;
}
