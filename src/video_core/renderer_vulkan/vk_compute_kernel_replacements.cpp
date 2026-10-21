// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_vulkan/vk_compute_kernel_replacements.h"
#include "video_core/renderer_vulkan/compute_dispatch_capture_plan.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <exception>
#include <string>

#include <cmrc/cmrc.hpp>

#include "common/hash.h"

CMRC_DECLARE(res);

namespace Vulkan {
namespace {

constexpr u32 SpirvMagic = 0x07230203;
constexpr u64 ReplacementCacheSalt = 0x434b52504c000001ULL;

bool IsDebugInstruction(u32 opcode) {
    switch (opcode) {
    case 2:   // OpSourceContinued
    case 3:   // OpSource
    case 4:   // OpSourceExtension
    case 5:   // OpName
    case 6:   // OpMemberName
    case 7:   // OpString
    case 8:   // OpLine
    case 317: // OpNoLine
    case 330: // OpModuleProcessed
        return true;
    default:
        return false;
    }
}

struct Instruction {
    std::size_t offset{};
    std::size_t word_count{};
    u32 opcode{};
};

bool NextInstruction(std::span<const u32> module, std::size_t& offset,
                     Instruction& instruction) {
    if (offset >= module.size()) {
        return false;
    }
    const u32 first_word = module[offset];
    const std::size_t word_count = first_word >> 16;
    if (word_count == 0 || word_count > module.size() - offset) {
        return false;
    }
    instruction = {.offset = offset,
                   .word_count = word_count,
                   .opcode = first_word & 0xffff};
    offset += word_count;
    return true;
}

bool IsContractInstruction(const Instruction& instruction, std::span<const u32> module) {
    const u32 opcode = instruction.opcode;
    if ((opcode >= 19 && opcode <= 39) || (opcode >= 224 && opcode <= 242) || opcode == 318 ||
        opcode == 319 || opcode == 10 || opcode == 11 || opcode == 14 || opcode == 15 ||
        opcode == 16 || opcode == 17 || opcode == 331 || opcode == 332 ||
        (opcode >= 72 && opcode <= 76) || opcode == 5632 || opcode == 5633) {
        return true;
    }
    if (opcode == 71) {
        if (instruction.word_count < 3) {
            return true;
        }
        return module[instruction.offset + 2] != 42; // NoContraction is checked per live result.
    }
    if (opcode == 59 && instruction.word_count >= 4) {
        return module[instruction.offset + 3] != 7; // Keep every module-scope variable.
    }
    return false;
}

std::vector<Instruction> CollectContractInstructions(std::span<const u32> module) {
    std::vector<Instruction> result;
    std::size_t offset = 5;
    while (offset < module.size()) {
        Instruction instruction{};
        if (!NextInstruction(module, offset, instruction)) {
            return {};
        }
        if (IsContractInstruction(instruction, module)) {
            result.push_back(instruction);
        }
    }
    return result;
}

std::vector<u32> CollectNoContractionTargets(std::span<const u32> module) {
    std::vector<u32> result;
    std::size_t offset = 5;
    while (offset < module.size()) {
        Instruction instruction{};
        if (!NextInstruction(module, offset, instruction)) {
            return {};
        }
        if (instruction.opcode == 71 && instruction.word_count >= 3 &&
            module[instruction.offset + 2] == 42) {
            result.push_back(module[instruction.offset + 1]);
        }
    }
    std::ranges::sort(result);
    return result;
}

bool HasExecutableReference(std::span<const u32> module, u32 id) {
    std::size_t offset = 5;
    while (offset < module.size()) {
        Instruction instruction{};
        if (!NextInstruction(module, offset, instruction)) {
            return true;
        }
        if (IsDebugInstruction(instruction.opcode) ||
            (instruction.opcode >= 71 && instruction.opcode <= 76) ||
            instruction.opcode == 332 || instruction.opcode == 5632 ||
            instruction.opcode == 5633) {
            continue;
        }
        for (std::size_t index = instruction.offset + 1;
             index < instruction.offset + instruction.word_count; ++index) {
            if (module[index] == id) {
                return true;
            }
        }
    }
    return false;
}

bool PreservesNoContraction(std::span<const u32> source,
                            std::span<const u32> replacement) {
    const auto source_targets = CollectNoContractionTargets(source);
    const auto replacement_targets = CollectNoContractionTargets(replacement);
    if (!std::ranges::includes(source_targets, replacement_targets)) {
        return false;
    }
    for (const u32 target : source_targets) {
        if (!std::binary_search(replacement_targets.begin(), replacement_targets.end(), target) &&
            HasExecutableReference(replacement, target)) {
            return false;
        }
    }
    return true;
}

bool ContractInstructionsMatch(std::span<const u32> source,
                               std::span<const u32> replacement) {
    const auto source_contract = CollectContractInstructions(source);
    const auto replacement_contract = CollectContractInstructions(replacement);
    if (source_contract.size() != replacement_contract.size()) {
        return false;
    }
    for (std::size_t index = 0; index < source_contract.size(); ++index) {
        const auto& left = source_contract[index];
        const auto& right = replacement_contract[index];
        if (left.word_count != right.word_count || left.opcode != right.opcode ||
            !std::equal(source.begin() + left.offset, source.begin() + left.offset + left.word_count,
                        replacement.begin() + right.offset)) {
            return false;
        }
    }
    return true;
}

std::vector<u32> LoadSpirvResource(std::string_view path) {
    const auto file = cmrc::res::get_filesystem().open(std::string{path});
    if (file.size() % sizeof(u32) != 0) {
        return {};
    }
    const auto* bytes = reinterpret_cast<const u8*>(file.begin());
    std::vector<u32> result(file.size() / sizeof(u32));
    for (std::size_t index = 0; index < result.size(); ++index) {
        const std::size_t byte = index * sizeof(u32);
        result[index] = u32{bytes[byte]} | (u32{bytes[byte + 1]} << 8) |
                        (u32{bytes[byte + 2]} << 16) | (u32{bytes[byte + 3]} << 24);
    }
    return result;
}

ComputeKernelReplacement MakeReplacement(std::string_view id, u64 guest_hash) {
    auto source_spirv = LoadSpirvResource(
        std::string{"src/resources/compute_kernels/"} + std::string{id} + ".source.spv");
    auto replacement_spirv = LoadSpirvResource(
        std::string{"src/resources/compute_kernels/"} + std::string{id} + ".replacement.spv");
    u64 content_identity = ReplacementCacheSalt;
    for (const u32 word : replacement_spirv) {
        content_identity = HashCombine(content_identity, static_cast<u64>(word));
    }
    ComputeKernelReplacement replacement{
        .id = id,
        .guest_hash = guest_hash,
        .version = 1,
        .cache_identity =
            HashCombine(HashCombine(HashCombine(ReplacementCacheSalt, guest_hash), u64{1}),
                        content_identity),
        .source_spirv = std::move(source_spirv),
        .replacement_spirv = std::move(replacement_spirv),
    };
    if (!MatchesCanonicalSpirv(replacement.source_spirv, replacement.source_spirv) ||
        !IsValidSpirvModule(replacement.replacement_spirv) ||
        !PreservesComputeShaderContract(replacement.source_spirv,
                                        replacement.replacement_spirv)) {
        return {};
    }
    return replacement;
}

char LowerHex(char value) {
    if (value >= 'A' && value <= 'F') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

std::string_view Trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
                              value.front() == '\r' || value.front() == '\n')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                              value.back() == '\r' || value.back() == '\n')) {
        value.remove_suffix(1);
    }
    return value;
}

bool HashTokenMatches(std::string_view token, std::string_view id) {
    token = Trim(token);
    if (token.starts_with("0x") || token.starts_with("0X")) {
        token.remove_prefix(2);
    }
    while (token.size() > id.size() && token.front() == '0') {
        token.remove_prefix(1);
    }
    if (token.size() != id.size()) {
        return false;
    }
    for (std::size_t index = 0; index < token.size(); ++index) {
        if (LowerHex(token[index]) != LowerHex(id[index])) {
            return false;
        }
    }
    return true;
}

} // namespace

ComputeKernelReplacementOptions ComputeKernelReplacementOptionsFromEnvironment() {
    const char* enabled = std::getenv("SHADPS4_ENABLE_COMPUTE_KERNEL_REPLACEMENTS");
    const char* allowlist = std::getenv("SHADPS4_COMPUTE_KERNEL_ALLOWLIST");
    return {
        .enabled = enabled && std::string_view{enabled} == "1",
        .allowlist = allowlist ? std::string_view{allowlist} : std::string_view{},
    };
}

bool IsComputeKernelReplacementModeActive(const ComputeKernelReplacementOptions& options) {
    if (!options.enabled) {
        return false;
    }
    const auto replacements = BuiltinComputeKernelReplacements();
    return std::ranges::any_of(replacements, [&](const auto& replacement) {
        return IsComputeKernelAllowlisted(replacement.id, options.allowlist);
    });
}

bool IsComputeCaptureRetentionEnabled(std::string_view capture_directory) {
    return !Trim(capture_directory).empty();
}

std::size_t AddComputeKernelIdentityToPipelineKey(std::size_t key,
                                                  u64 replacement_identity) {
    if (replacement_identity == 0) {
        return key;
    }
    return static_cast<std::size_t>(
        HashCombine(static_cast<u64>(key), replacement_identity));
}

std::span<const ComputeKernelReplacement> BuiltinComputeKernelReplacements() {
    static const std::vector<ComputeKernelReplacement> replacements = [] {
        try {
            std::vector<ComputeKernelReplacement> result;
            for (auto candidate : {MakeReplacement("1d99a308", 0x1d99a308),
                                   MakeReplacement("ee29a0b1", 0xee29a0b1)}) {
                if (candidate.source_spirv.empty() || candidate.replacement_spirv.empty()) {
                    return std::vector<ComputeKernelReplacement>{};
                }
                result.push_back(std::move(candidate));
            }
            return result;
        } catch (const std::exception&) {
            return std::vector<ComputeKernelReplacement>{};
        }
    }();
    return replacements;
}

const ComputeKernelReplacement* SelectComputeKernelReplacement(
    bool is_compute_shader, u64 guest_hash, std::span<const u32> source_spirv,
    std::span<const ComputeKernelReplacement> replacements,
    const ComputeKernelReplacementOptions& options) {
    if (!is_compute_shader || !options.enabled) {
        return nullptr;
    }
    for (const auto& replacement : replacements) {
        if (replacement.guest_hash != guest_hash ||
            !IsComputeKernelAllowlisted(replacement.id, options.allowlist) ||
            !MatchesCanonicalSpirv(source_spirv, replacement.source_spirv) ||
            !PreservesComputeShaderContract(replacement.source_spirv,
                                            replacement.replacement_spirv)) {
            continue;
        }
        return &replacement;
    }
    return nullptr;
}

bool IsComputeKernelAllowlisted(std::string_view id, std::string_view allowlist) {
    while (!allowlist.empty()) {
        const std::size_t comma = allowlist.find(',');
        const std::string_view token = allowlist.substr(0, comma);
        if (HashTokenMatches(token, id)) {
            return true;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        allowlist.remove_prefix(comma + 1);
    }
    return false;
}

bool IsComputeKernelCaptureTarget(u64 guest_hash) {
    return std::ranges::find(ComputeCapture::CaptureTargetHashes, guest_hash) !=
           ComputeCapture::CaptureTargetHashes.end();
}

bool IsValidSpirvModule(std::span<const u32> module) {
    if (module.size() < 5 || module[0] != SpirvMagic || module[3] == 0 || module[4] != 0) {
        return false;
    }
    std::size_t offset = 5;
    while (offset < module.size()) {
        Instruction instruction{};
        if (!NextInstruction(module, offset, instruction)) {
            return false;
        }
    }
    return offset == module.size();
}

bool MatchesCanonicalSpirv(std::span<const u32> source,
                           std::span<const u32> canonical_source) {
    if (!IsValidSpirvModule(source) || !IsValidSpirvModule(canonical_source) ||
        source[0] != canonical_source[0] || source[1] != canonical_source[1] ||
        source[2] != canonical_source[2] ||
        source[4] != canonical_source[4]) {
        return false;
    }

    std::size_t source_offset = 5;
    std::size_t canonical_offset = 5;
    while (source_offset < source.size()) {
        Instruction source_instruction{};
        if (!NextInstruction(source, source_offset, source_instruction)) {
            return false;
        }
        if (IsDebugInstruction(source_instruction.opcode)) {
            continue;
        }
        Instruction canonical_instruction{};
        if (!NextInstruction(canonical_source, canonical_offset, canonical_instruction) ||
            IsDebugInstruction(canonical_instruction.opcode) ||
            source_instruction.word_count != canonical_instruction.word_count ||
            source_instruction.opcode != canonical_instruction.opcode ||
            !std::equal(source.begin() + source_instruction.offset,
                        source.begin() + source_instruction.offset + source_instruction.word_count,
                        canonical_source.begin() + canonical_instruction.offset)) {
            return false;
        }
    }
    return canonical_offset == canonical_source.size();
}

bool PreservesComputeShaderContract(std::span<const u32> source,
                                    std::span<const u32> replacement) {
    if (!IsValidSpirvModule(source) || !IsValidSpirvModule(replacement) ||
        source[0] != replacement[0] || source[1] != replacement[1] ||
        source[2] != replacement[2] || source[4] != replacement[4] ||
        !ContractInstructionsMatch(source, replacement)) {
        return false;
    }
    return PreservesNoContraction(source, replacement);
}

} // namespace Vulkan
