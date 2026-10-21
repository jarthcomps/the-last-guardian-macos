// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <algorithm>
#include <array>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

#include "video_core/renderer_vulkan/vk_compute_kernel_replacements.h"

namespace {

int failures{};

#define CHECK(condition)                                                                          \
    do {                                                                                          \
        if (!(condition)) {                                                                      \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,             \
                         #condition);                                                             \
            ++failures;                                                                           \
        }                                                                                         \
    } while (false)

void AddInstruction(std::vector<u32>& module, u32 opcode,
                    std::initializer_list<u32> operands) {
    module.push_back((static_cast<u32>(operands.size() + 1) << 16) | opcode);
    module.insert(module.end(), operands.begin(), operands.end());
}

bool ChangeOperand(std::vector<u32>& module, u32 opcode, std::size_t operand_index,
                   u32 value) {
    for (std::size_t offset = 5; offset < module.size();) {
        const std::size_t word_count = module[offset] >> 16;
        if ((module[offset] & 0xffff) == opcode && word_count > operand_index + 1) {
            module[offset + operand_index + 1] = value;
            return true;
        }
        offset += word_count;
    }
    return false;
}

std::vector<u32> MakeModule(bool add_debug_name = false) {
    std::vector<u32> module{0x07230203, 0x00010600, 0, 64, 0};
    AddInstruction(module, 17, {1});                         // OpCapability Shader
    AddInstruction(module, 14, {0, 1});                     // OpMemoryModel
    AddInstruction(module, 15, {5, 3, 0x6e69616d, 0, 4});   // OpEntryPoint "main"
    AddInstruction(module, 16, {3, 17, 64, 1, 1});           // OpExecutionMode LocalSize
    if (add_debug_name) {
        AddInstruction(module, 5, {3, 0x6168706c, 0});       // OpName %3 "alpha"
        AddInstruction(module, 7, {1, 0x74736574, 0});       // OpString "test"
    }
    AddInstruction(module, 71, {4, 33, 2});                  // OpDecorate Binding 2
    AddInstruction(module, 71, {4, 34, 0});                  // OpDecorate DescriptorSet 0
    AddInstruction(module, 71, {50, 42});                    // OpDecorate NoContraction
    AddInstruction(module, 19, {2});                         // OpTypeVoid
    AddInstruction(module, 32, {6, 12, 2});                  // OpTypePointer StorageBuffer
    AddInstruction(module, 59, {6, 4, 12});                  // OpVariable StorageBuffer
    AddInstruction(module, 129, {2, 50, 4, 4});             // OpFAdd
    return module;
}

void TestCanonicalMatching() {
    auto source = MakeModule(true);
    auto canonical = MakeModule(false);

    CHECK(Vulkan::IsValidSpirvModule(source));
    CHECK(Vulkan::IsValidSpirvModule(canonical));
    CHECK(Vulkan::MatchesCanonicalSpirv(source, canonical));

    auto different_code = source;
    different_code[6] ^= 1;
    CHECK(!Vulkan::MatchesCanonicalSpirv(different_code, canonical));

    auto debug_only_change = source;
    for (std::size_t offset = 5; offset < debug_only_change.size();) {
        const std::size_t word_count = debug_only_change[offset] >> 16;
        if ((debug_only_change[offset] & 0xffff) == 5) {
            debug_only_change[offset + 2] ^= 0x20;
            break;
        }
        offset += word_count;
    }
    CHECK(Vulkan::MatchesCanonicalSpirv(debug_only_change, canonical));

    auto different_version = source;
    different_version[1]++;
    CHECK(!Vulkan::MatchesCanonicalSpirv(different_version, canonical));

    auto malformed = source;
    malformed[5] = 0;
    CHECK(!Vulkan::IsValidSpirvModule(malformed));
    CHECK(!Vulkan::MatchesCanonicalSpirv(malformed, canonical));

    auto truncated = source;
    truncated.pop_back();
    CHECK(!Vulkan::IsValidSpirvModule(truncated));
    auto oversized_instruction = source;
    oversized_instruction[5] = (0xffffu << 16) | (oversized_instruction[5] & 0xffff);
    CHECK(!Vulkan::IsValidSpirvModule(oversized_instruction));

    auto alternate_bound = canonical;
    alternate_bound[3] = 80;
    CHECK(Vulkan::MatchesCanonicalSpirv(source, alternate_bound));
    auto invalid_bound = canonical;
    invalid_bound[3] = 0;
    CHECK(!Vulkan::IsValidSpirvModule(invalid_bound));
}

void TestContractChecks() {
    const auto source = MakeModule();
    CHECK(Vulkan::PreservesComputeShaderContract(source, source));

    auto different_binding = source;
    for (std::size_t offset = 5; offset < different_binding.size();) {
        const u32 word_count = different_binding[offset] >> 16;
        const u32 opcode = different_binding[offset] & 0xffff;
        if (opcode == 71 && word_count >= 4 && different_binding[offset + 2] == 33) {
            different_binding[offset + 3] = 3;
            break;
        }
        offset += word_count;
    }
    CHECK(!Vulkan::PreservesComputeShaderContract(source, different_binding));

    auto different_descriptor_set = source;
    bool changed_set = false;
    for (std::size_t offset = 5; offset < different_descriptor_set.size();) {
        const std::size_t word_count = different_descriptor_set[offset] >> 16;
        if ((different_descriptor_set[offset] & 0xffff) == 71 && word_count >= 4 &&
            different_descriptor_set[offset + 2] == 34) {
            different_descriptor_set[offset + 3] = 1;
            changed_set = true;
            break;
        }
        offset += word_count;
    }
    CHECK(changed_set);
    CHECK(!Vulkan::PreservesComputeShaderContract(source, different_descriptor_set));

    auto different_capability = source;
    CHECK(ChangeOperand(different_capability, 17, 0, 2));
    CHECK(!Vulkan::PreservesComputeShaderContract(source, different_capability));

    auto different_memory_model = source;
    CHECK(ChangeOperand(different_memory_model, 14, 0, 1));
    CHECK(!Vulkan::PreservesComputeShaderContract(source, different_memory_model));

    auto different_local_size = source;
    for (std::size_t offset = 5; offset < different_local_size.size();) {
        const u32 word_count = different_local_size[offset] >> 16;
        const u32 opcode = different_local_size[offset] & 0xffff;
        if (opcode == 16) {
            different_local_size[offset + 3] = 32;
            break;
        }
        offset += word_count;
    }
    CHECK(!Vulkan::PreservesComputeShaderContract(source, different_local_size));

    auto missing_no_contraction = source;
    for (std::size_t offset = 5; offset < missing_no_contraction.size();) {
        const u32 word_count = missing_no_contraction[offset] >> 16;
        const u32 opcode = missing_no_contraction[offset] & 0xffff;
        if (opcode == 71 && word_count == 3 && missing_no_contraction[offset + 2] == 42) {
            missing_no_contraction.erase(missing_no_contraction.begin() + offset,
                                         missing_no_contraction.begin() + offset + word_count);
            break;
        }
        offset += word_count;
    }
    CHECK(!Vulkan::PreservesComputeShaderContract(source, missing_no_contraction));

    auto atomic_source = source;
    AddInstruction(atomic_source, 234, {2, 51, 4, 1, 8, 4}); // OpAtomicIAdd
    auto changed_atomic_semantics = atomic_source;
    CHECK(ChangeOperand(changed_atomic_semantics, 234, 4, 16));
    CHECK(!Vulkan::PreservesComputeShaderContract(atomic_source, changed_atomic_semantics));

    auto dead_arithmetic = source;
    std::size_t arithmetic_offset = dead_arithmetic.size();
    for (std::size_t offset = 5; offset < dead_arithmetic.size();) {
        const std::size_t word_count = dead_arithmetic[offset] >> 16;
        const u32 opcode = dead_arithmetic[offset] & 0xffff;
        if (opcode == 129) {
            arithmetic_offset = offset;
            break;
        }
        offset += word_count;
    }
    CHECK(arithmetic_offset != dead_arithmetic.size());
    if (arithmetic_offset != dead_arithmetic.size()) {
        const std::size_t word_count = dead_arithmetic[arithmetic_offset] >> 16;
        dead_arithmetic.erase(dead_arithmetic.begin() + arithmetic_offset,
                              dead_arithmetic.begin() + arithmetic_offset + word_count);
        for (std::size_t at = 5; at < dead_arithmetic.size();) {
            const u32 count = dead_arithmetic[at] >> 16;
            const u32 opcode = dead_arithmetic[at] & 0xffff;
            if (opcode == 71 && count == 3 && dead_arithmetic[at + 2] == 42) {
                dead_arithmetic.erase(dead_arithmetic.begin() + at,
                                      dead_arithmetic.begin() + at + count);
                break;
            }
            at += count;
        }
        CHECK(Vulkan::PreservesComputeShaderContract(source, dead_arithmetic));
    }
}

void TestAllowlistAndSelector() {
    const Vulkan::ComputeKernelReplacementOptions defaults{};
    CHECK(!defaults.enabled);
    CHECK(defaults.allowlist.empty());
    CHECK(!Vulkan::IsComputeKernelReplacementModeActive(defaults));
    CHECK(Vulkan::IsComputeKernelAllowlisted("1d99a308", "1d99a308"));
    CHECK(Vulkan::IsComputeKernelAllowlisted("1d99a308", "ee29a0b1, 0x000000001D99A308"));
    CHECK(!Vulkan::IsComputeKernelAllowlisted("1d99a308", "1d99a30"));
    CHECK(!Vulkan::IsComputeKernelAllowlisted("1d99a308", "x1d99a308"));
    CHECK(!Vulkan::IsComputeKernelAllowlisted("1d99a308", "*"));
    CHECK(Vulkan::IsComputeCaptureRetentionEnabled("/tmp/compute-captures"));
    CHECK(!Vulkan::IsComputeCaptureRetentionEnabled("  \t\n"));
    CHECK(!Vulkan::IsComputeKernelReplacementModeActive(
        Vulkan::ComputeKernelReplacementOptions{.enabled = false, .allowlist = "1d99a308"}));
    CHECK(Vulkan::IsComputeKernelReplacementModeActive(
        Vulkan::ComputeKernelReplacementOptions{.enabled = true, .allowlist = "1d99a308"}));
    CHECK(!Vulkan::IsComputeKernelReplacementModeActive(
        Vulkan::ComputeKernelReplacementOptions{.enabled = true, .allowlist = "deadbeef"}));
    const std::size_t base_key = 0x12345678;
    CHECK(Vulkan::AddComputeKernelIdentityToPipelineKey(base_key, 0) == base_key);
    CHECK(Vulkan::AddComputeKernelIdentityToPipelineKey(base_key, 0x1234) ==
          Vulkan::AddComputeKernelIdentityToPipelineKey(base_key, 0x1234));
    CHECK(Vulkan::AddComputeKernelIdentityToPipelineKey(base_key, 0x1234) != base_key);
    CHECK(Vulkan::AddComputeKernelIdentityToPipelineKey(base_key, 0x1234) !=
          Vulkan::AddComputeKernelIdentityToPipelineKey(base_key, 0x5678));

    std::vector<u32> source = MakeModule();
    std::vector<u32> replacement = source;
    Vulkan::ComputeKernelReplacement asset{
        .id = "1d99a308",
        .guest_hash = 0x1d99a308,
        .version = 1,
        .cache_identity = 0x1234,
        .source_spirv = source,
        .replacement_spirv = replacement,
    };
    const std::array assets{asset};
    const Vulkan::ComputeKernelReplacementOptions enabled{
        .enabled = true,
        .allowlist = "1d99a308",
    };
    const Vulkan::ComputeKernelReplacementOptions disabled{
        .enabled = false,
        .allowlist = "1d99a308",
    };
    CHECK(Vulkan::SelectComputeKernelReplacement(true, 0x1d99a308, source, assets, enabled) ==
          &assets[0]);
    CHECK(Vulkan::SelectComputeKernelReplacement(true, 0x1d99a308, source, assets, enabled) ==
          Vulkan::SelectComputeKernelReplacement(true, 0x1d99a308, source, assets, enabled));
    CHECK(Vulkan::SelectComputeKernelReplacement(true, 0x1d99a308, source, assets, disabled) ==
          nullptr);
    CHECK(Vulkan::SelectComputeKernelReplacement(
              true, 0x1d99a308, source, assets,
              Vulkan::ComputeKernelReplacementOptions{.enabled = true, .allowlist = "1d99a30"}) ==
          nullptr);
    CHECK(Vulkan::SelectComputeKernelReplacement(false, 0x1d99a308, source, assets, enabled) ==
          nullptr);
    CHECK(Vulkan::SelectComputeKernelReplacement(true, 0xee29a0b1, source, assets, enabled) ==
          nullptr);
    auto changed = source;
    changed[6] ^= 1;
    CHECK(Vulkan::SelectComputeKernelReplacement(true, 0x1d99a308, changed, assets, enabled) ==
          nullptr);
}

void TestBuiltinReplacements() {
    const auto replacements = Vulkan::BuiltinComputeKernelReplacements();
    CHECK(replacements.size() == 2);
    for (const auto& replacement : replacements) {
        CHECK(replacement.version == 1);
        CHECK(replacement.cache_identity != 0);
        CHECK(Vulkan::IsValidSpirvModule(replacement.source_spirv));
        CHECK(Vulkan::IsValidSpirvModule(replacement.replacement_spirv));
        CHECK(Vulkan::MatchesCanonicalSpirv(replacement.source_spirv,
                                            replacement.source_spirv));
        CHECK(Vulkan::PreservesComputeShaderContract(replacement.source_spirv,
                                                     replacement.replacement_spirv));
        CHECK(replacement.source_spirv != replacement.replacement_spirv);

        const Vulkan::ComputeKernelReplacementOptions options{
            .enabled = true,
            .allowlist = replacement.id,
        };
        CHECK(Vulkan::SelectComputeKernelReplacement(true, replacement.guest_hash,
                                                      replacement.source_spirv, replacements,
                                                      options) == &replacement);
        auto changed = replacement.source_spirv;
        changed[6] ^= 1;
        CHECK(Vulkan::SelectComputeKernelReplacement(true, replacement.guest_hash, changed,
                                                      replacements, options) == nullptr);
    }

    CHECK(Vulkan::IsComputeKernelCaptureTarget(0xa3c4525b));
    CHECK(Vulkan::IsComputeKernelCaptureTarget(0xeb218825));
    CHECK(Vulkan::IsComputeKernelCaptureTarget(0x1d99a308));
    CHECK(Vulkan::IsComputeKernelCaptureTarget(0x1171c360));
    CHECK(Vulkan::IsComputeKernelCaptureTarget(0xee29a0b1));
    CHECK(!Vulkan::IsComputeKernelCaptureTarget(0xdeadbeef));
}

} // namespace

int main() {
    TestCanonicalMatching();
    TestContractChecks();
    TestAllowlistAndSelector();
    TestBuiltinReplacements();
    return failures == 0 ? 0 : 1;
}
