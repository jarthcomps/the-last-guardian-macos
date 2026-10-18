// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include "video_core/renderer_vulkan/vk_command_recorder.h"

namespace Vulkan::ComputeDiagnostic {
inline bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("SHADPS4_COMPUTE_DISPATCH_LABELS");
        return value && std::string_view{value} == "1";
    }();
    return enabled;
}
inline std::string GuestLabel(uint64_t hash, bool indirect, std::array<u32, 3> groups,
                              std::array<u32, 3> local) {
    char text[192];
    std::snprintf(text, sizeof(text),
                  "guest_cs_%016llx %s groups=%ux%ux%u local=%ux%ux%u",
                  static_cast<unsigned long long>(hash), indirect ? "indirect" : "direct",
                  groups[0], groups[1], groups[2], local[0], local[1], local[2]);
    return text;
}
inline std::string TilingLabel(bool tiler, u32 mode, u32 bits, u32 mips, u32 slices, u32 groups) {
    char text[192];
    std::snprintf(text, sizeof(text), "host_%s mode=%u bits=%u mips=%u slices=%u groups=%u",
                  tiler ? "tiler" : "detiler", mode, bits, mips, slices, groups);
    return text;
}
class Scope {
public:
    Scope(CommandRecorder recorder_, const std::string& label) : recorder{recorder_} {
        active = !label.empty() &&
                 vk::detail::defaultDispatchLoaderDynamic.vkCmdBeginDebugUtilsLabelEXT &&
                 vk::detail::defaultDispatchLoaderDynamic.vkCmdEndDebugUtilsLabelEXT;
        if (active) recorder.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{.pLabelName = label.c_str()});
    }
    ~Scope() { if (active) recorder.endDebugUtilsLabelEXT(); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    CommandRecorder recorder;
    bool active{};
};
} // namespace Vulkan::ComputeDiagnostic
