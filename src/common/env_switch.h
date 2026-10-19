// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
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

/// A number setting an environment variable may override: a plain decimal number replaces
/// `fallback`; anything else (or no variable) keeps it.
constexpr std::uint32_t EnvOverrideNumber(const char* value, std::uint32_t fallback) {
    if (value == nullptr || *value == 0) {
        return fallback;
    }
    std::uint64_t number = 0;
    for (const char* c = value; *c != 0; ++c) {
        if (*c < '0' || *c > '9') {
            return fallback;
        }
        number = number * 10 + static_cast<std::uint64_t>(*c - '0');
        if (number > 0xffffffffULL) {
            return fallback;
        }
    }
    return static_cast<std::uint32_t>(number);
}

inline std::uint32_t EnvOverrideNumberNamed(const char* name, std::uint32_t fallback) {
    return EnvOverrideNumber(std::getenv(name), fallback);
}

} // namespace Common
