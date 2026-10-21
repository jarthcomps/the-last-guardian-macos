// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "video_core/renderer_vulkan/compute_dispatch_capture_plan.h"

using namespace Vulkan::ComputeCapture;

int main() {
    // SHADPS4_CAPTURE_COMPUTE_LIMIT: 1 when unset or empty, otherwise clamped to 1 to 8.
    assert(CaptureCountLimit(nullptr) == 1);
    assert(CaptureCountLimit("") == 1);
    assert(CaptureCountLimit("1") == 1);
    assert(CaptureCountLimit("3") == 3);
    assert(CaptureCountLimit("8") == 8);
    assert(CaptureCountLimit("9") == 8);
    assert(CaptureCountLimit("0") == 1);
    assert(CaptureCountLimit("-4") == 1);
    assert(CaptureCountLimit("x") == 1);
    assert(CaptureCountLimit("2x") == 2);
    assert(CaptureCountLimit(" 5") == 5);
    assert(CaptureCountLimit("99999999999999999999") == 8);

    // An unset directory never counts as capture. A set directory counts once it holds the marker.
    assert(!IsCaptureEnabled(std::filesystem::path{}));
    const auto directory = std::filesystem::temp_directory_path() /
        ("shadps4-compute-capture-env-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    assert(!IsCaptureEnabled(directory));
    assert(!IsCaptureEnabled(directory / "missing"));
    std::ofstream(directory / CaptureArmMarkerName) << "ready\n";
    assert(IsCaptureEnabled(directory));
    std::filesystem::remove_all(directory);
    assert(!IsCaptureEnabled(directory));
    return 0;
}
