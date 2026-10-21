// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "common/types.h"

namespace Vulkan {

struct ComputeKernelReplacement {
    std::string_view id;
    u64 guest_hash{};
    u32 version{};
    u64 cache_identity{};
    std::vector<u32> source_spirv;
    std::vector<u32> replacement_spirv;
};

struct ComputeKernelReplacementOptions {
    bool enabled{};
    std::string_view allowlist;
};

ComputeKernelReplacementOptions ComputeKernelReplacementOptionsFromEnvironment();
bool IsComputeKernelReplacementModeActive(const ComputeKernelReplacementOptions& options);
bool IsComputeCaptureRetentionEnabled(std::string_view capture_directory);
std::size_t AddComputeKernelIdentityToPipelineKey(std::size_t key, u64 replacement_identity);

std::span<const ComputeKernelReplacement> BuiltinComputeKernelReplacements();

const ComputeKernelReplacement* SelectComputeKernelReplacement(
    bool is_compute_shader, u64 guest_hash, std::span<const u32> source_spirv,
    std::span<const ComputeKernelReplacement> replacements,
    const ComputeKernelReplacementOptions& options);

bool IsComputeKernelAllowlisted(std::string_view id, std::string_view allowlist);
bool IsComputeKernelCaptureTarget(u64 guest_hash);
bool IsValidSpirvModule(std::span<const u32> module);
bool MatchesCanonicalSpirv(std::span<const u32> source,
                           std::span<const u32> canonical_source);
bool PreservesComputeShaderContract(std::span<const u32> source,
                                   std::span<const u32> replacement);

} // namespace Vulkan
