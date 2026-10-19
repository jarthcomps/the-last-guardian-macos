// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdlib>
#include <string_view>

namespace Common {

/// A boolean setting that an environment variable may override: "1" turns it on, "0" turns it
/// off, anything else (or no variable) keeps `fallback`. Lets the launcher switch an option that
/// otherwise lives in a game's config file.
constexpr bool EnvOverride(const char* value, bool fallback) {
    if (value == nullptr) {
        return fallback;
    }
    const std::string_view text{value};
    if (text == "1") {
        return true;
    }
    if (text == "0") {
        return false;
    }
    return fallback;
}

inline bool EnvOverrideNamed(const char* name, bool fallback) {
    return EnvOverride(std::getenv(name), fallback);
}

} // namespace Common
