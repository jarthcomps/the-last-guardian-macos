// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Vulkan::ShaderDiagnostic {

inline constexpr std::string_view EnvironmentVariable = "SHADPS4_SHADER_DIAGNOSTIC_NAMES";

inline bool NamesEnabled() noexcept {
    const char* value = std::getenv(EnvironmentVariable.data());
    return value != nullptr && std::string_view{value} == "1";
}

inline std::filesystem::path CacheRoot(std::filesystem::path root, bool diagnostic_names) {
    if (diagnostic_names) {
        root /= "shader-diagnostics";
    }
    return root;
}

inline bool IsHexDigit(char value) noexcept {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
}

inline bool IsDecimal(std::string_view value, bool allow_zero = true) noexcept {
    if (value.empty() || (value.size() > 1 && value.front() == '0')) {
        return false;
    }
    if (!allow_zero && value == "0") {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; });
}

inline bool IsModeToken(std::string_view value) noexcept {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    });
}

inline bool IsGuestName(std::string_view name) noexcept {
    constexpr std::string_view prefix = "shadps4_guest_";
    if (!name.starts_with(prefix)) {
        return false;
    }
    name.remove_prefix(prefix.size());
    const auto stage_end = name.find('_');
    if (stage_end == std::string_view::npos) {
        return false;
    }
    constexpr std::array stages = {"Fragment", "Vertex", "Geometry", "Export", "Hull", "Local",
                                   "Compute"};
    if (std::ranges::find(stages, name.substr(0, stage_end)) == stages.end()) {
        return false;
    }
    name.remove_prefix(stage_end + 1);
    if (name.size() < 16 + 3 || !std::all_of(name.begin(), name.begin() + 16, IsHexDigit) ||
        name.substr(16, 2) != "_p") {
        return false;
    }
    name.remove_prefix(18);
    return IsDecimal(name);
}

inline bool IsHostTilingName(std::string_view name) noexcept {
    constexpr std::string_view prefix = "shadps4_host_tiling_";
    if (!name.starts_with(prefix)) {
        return false;
    }
    name.remove_prefix(prefix.size());
    const auto mode_end = name.find('_');
    if (mode_end == std::string_view::npos || !IsModeToken(name.substr(0, mode_end))) {
        return false;
    }
    name.remove_prefix(mode_end + 1);
    const auto bpp_end = name.find('_');
    if (bpp_end == std::string_view::npos || !IsDecimal(name.substr(0, bpp_end), false)) {
        return false;
    }
    const auto bpp = name.substr(0, bpp_end);
    if (bpp != "8" && bpp != "16" && bpp != "32" && bpp != "64" && bpp != "96" &&
        bpp != "128") {
        return false;
    }
    name.remove_prefix(bpp_end + 1);
    const auto direction_end = name.find('_');
    if (direction_end == std::string_view::npos) {
        return false;
    }
    const auto direction = name.substr(0, direction_end);
    name.remove_prefix(direction_end + 1);
    return (direction == "tiler" || direction == "detiler") &&
           (name == "generic" || name == "single");
}

inline bool IsDiagnosticName(std::string_view name) noexcept {
    return name.size() < 128 && (IsGuestName(name) || IsHostTilingName(name));
}

inline std::string GuestName(std::string_view stage, std::uint64_t hash, std::size_t permutation) {
    if (stage != "Fragment" && stage != "Vertex" && stage != "Geometry" && stage != "Export" &&
        stage != "Hull" && stage != "Local" && stage != "Compute") {
        return {};
    }
    std::array<char, 16> hash_text{};
    constexpr char digits[] = "0123456789abcdef";
    for (std::size_t i = 0; i < hash_text.size(); ++i) {
        const auto shift = static_cast<unsigned>((hash_text.size() - i - 1) * 4);
        hash_text[i] = digits[(hash >> shift) & 0xf];
    }
    std::string result{"shadps4_guest_"};
    result.append(stage);
    result.push_back('_');
    result.append(hash_text.data(), hash_text.size());
    result.append("_p");
    result.append(std::to_string(permutation));
    return result;
}

inline std::string HostTilingName(std::string_view mode, std::uint32_t bits_per_pixel,
                                  bool tiler, bool single_mip) {
    if (!IsModeToken(mode) || (bits_per_pixel != 8 && bits_per_pixel != 16 && bits_per_pixel != 32 &&
                               bits_per_pixel != 64 && bits_per_pixel != 96 &&
                               bits_per_pixel != 128)) {
        return {};
    }
    std::string result{"shadps4_host_tiling_"};
    result.append(mode);
    result.push_back('_');
    result.append(std::to_string(bits_per_pixel));
    result.append(tiler ? "_tiler_" : "_detiler_");
    result.append(single_mip ? "single" : "generic");
    return result.size() < 128 ? result : std::string{};
}

enum class OpNameResult {
    Success,
    InvalidName,
    InvalidHeader,
    MalformedInstruction,
    MalformedString,
    InvalidEntryPoint,
    AmbiguousEntryPoint,
    DuplicateEntryPointName,
};

namespace detail {

inline constexpr std::uint32_t SpirvMagic = 0x07230203;
inline constexpr std::uint16_t OpSourceContinued = 2;
inline constexpr std::uint16_t OpSource = 3;
inline constexpr std::uint16_t OpSourceExtension = 4;
inline constexpr std::uint16_t OpName = 5;
inline constexpr std::uint16_t OpEntryPoint = 15;
inline constexpr std::uint16_t OpExecutionMode = 16;
inline constexpr std::uint16_t OpCapability = 17;
inline constexpr std::uint16_t OpExtension = 10;
inline constexpr std::uint16_t OpExtInstImport = 11;
inline constexpr std::uint16_t OpMemoryModel = 14;
inline constexpr std::uint16_t OpExecutionModeId = 331;

struct LiteralString {
    std::string value;
    std::size_t word_count{};
};

inline bool IsValidUtf8(std::string_view value) noexcept {
    for (std::size_t i = 0; i < value.size();) {
        const auto first = static_cast<unsigned char>(value[i]);
        if (first <= 0x7f) {
            ++i;
            continue;
        }
        std::size_t length{};
        std::uint32_t code_point{};
        if ((first & 0xe0) == 0xc0) {
            length = 2;
            code_point = first & 0x1f;
            if (code_point < 2) {
                return false;
            }
        } else if ((first & 0xf0) == 0xe0) {
            length = 3;
            code_point = first & 0x0f;
        } else if ((first & 0xf8) == 0xf0) {
            length = 4;
            code_point = first & 0x07;
        } else {
            return false;
        }
        if (length > value.size() - i) {
            return false;
        }
        for (std::size_t j = 1; j < length; ++j) {
            const auto next = static_cast<unsigned char>(value[i + j]);
            if ((next & 0xc0) != 0x80) {
                return false;
            }
            code_point = (code_point << 6) | (next & 0x3f);
        }
        if ((length == 3 && code_point < 0x800) || (length == 4 && code_point < 0x10000) ||
            code_point > 0x10ffff || (code_point >= 0xd800 && code_point <= 0xdfff)) {
            return false;
        }
        i += length;
    }
    return true;
}

inline bool DecodeLiteralString(std::span<const std::uint32_t> words, bool require_all_words,
                                LiteralString& result) {
    result = {};
    for (std::size_t i = 0; i < words.size(); ++i) {
        for (unsigned byte_index = 0; byte_index < 4; ++byte_index) {
            const auto byte = static_cast<unsigned char>((words[i] >> (byte_index * 8)) & 0xff);
            if (byte == 0) {
                const auto trailing_mask = byte_index == 3
                                               ? 0u
                                               : ~((std::uint32_t{1} << ((byte_index + 1) * 8)) - 1);
                if ((words[i] & trailing_mask) != 0) {
                    return false;
                }
                result.word_count = i + 1;
                if ((require_all_words && result.word_count != words.size()) ||
                    !IsValidUtf8(result.value)) {
                    return false;
                }
                return true;
            }
            result.value.push_back(static_cast<char>(byte));
        }
    }
    return false;
}

inline std::vector<std::uint32_t> EncodeLiteralString(std::string_view value) {
    std::vector<std::uint32_t> words((value.size() + 1 + 3) / 4, 0);
    for (std::size_t i = 0; i < value.size(); ++i) {
        words[i / 4] |= static_cast<std::uint32_t>(static_cast<unsigned char>(value[i]))
                        << ((i % 4) * 8);
    }
    return words;
}

inline bool IsDebugNameProlog(std::uint16_t opcode) noexcept {
    return opcode == OpCapability || opcode == OpExtension || opcode == OpExtInstImport ||
           opcode == OpMemoryModel || opcode == OpEntryPoint || opcode == OpExecutionMode ||
           opcode == OpExecutionModeId || opcode == OpSourceContinued || opcode == OpSource ||
           opcode == OpSourceExtension;
}

} // namespace detail

// Adds or replaces the OpName of the sole OpEntryPoint function. The input vector is only swapped
// after the complete module has been validated and a replacement vector has been built.
inline OpNameResult SetEntryPointOpName(std::vector<std::uint32_t>& module,
                                        std::string_view diagnostic_name) {
    if (!IsDiagnosticName(diagnostic_name)) {
        return OpNameResult::InvalidName;
    }
    if (module.size() < 5 || module[0] != detail::SpirvMagic || module[3] == 0 || module[4] != 0) {
        return OpNameResult::InvalidHeader;
    }

    std::size_t entry_point_count{};
    std::uint32_t function_id{};
    std::size_t existing_name_offset = std::numeric_limits<std::size_t>::max();
    std::size_t existing_name_count{};
    std::size_t function_definition_count{};
    std::size_t insertion_offset = module.size();
    std::vector<std::size_t> name_offsets;

    for (std::size_t offset = 5; offset < module.size();) {
        const std::uint32_t instruction = module[offset];
        const auto word_count = static_cast<std::size_t>(instruction >> 16);
        const auto opcode = static_cast<std::uint16_t>(instruction & 0xffff);
        if (word_count == 0 || word_count > module.size() - offset) {
            return OpNameResult::MalformedInstruction;
        }
        const auto end = offset + word_count;

        if (opcode == detail::OpEntryPoint) {
            ++entry_point_count;
            if (word_count < 4) {
                return OpNameResult::InvalidEntryPoint;
            }
            detail::LiteralString entry_name{};
            if (!detail::DecodeLiteralString(
                    std::span<const std::uint32_t>{module}.subspan(offset + 3, end - offset - 3),
                    false, entry_name)) {
                return OpNameResult::MalformedString;
            }
            function_id = module[offset + 2];
            if (function_id == 0 || function_id >= module[3]) {
                return OpNameResult::InvalidEntryPoint;
            }
        } else if (opcode == detail::OpName) {
            if (word_count < 3) {
                return OpNameResult::MalformedString;
            }
            detail::LiteralString debug_name{};
            if (!detail::DecodeLiteralString(
                    std::span<const std::uint32_t>{module}.subspan(offset + 2, end - offset - 2),
                    true, debug_name)) {
                return OpNameResult::MalformedString;
            }
            if (module[offset + 1] == 0 || module[offset + 1] >= module[3]) {
                return OpNameResult::InvalidEntryPoint;
            }
            if (insertion_offset == module.size()) {
                insertion_offset = offset;
            }
            name_offsets.push_back(offset);
        } else if (opcode == 54) { // OpFunction
            if (word_count < 5) {
                return OpNameResult::MalformedInstruction;
            }
            if (module[offset + 2] == function_id) {
                ++function_definition_count;
            }
        } else if (!detail::IsDebugNameProlog(opcode) && insertion_offset == module.size()) {
            insertion_offset = offset;
        }
        offset = end;
    }

    if (entry_point_count == 0) {
        return OpNameResult::InvalidEntryPoint;
    }
    if (entry_point_count != 1) {
        return OpNameResult::AmbiguousEntryPoint;
    }
    for (const auto offset : name_offsets) {
        if (module[offset + 1] == function_id) {
            ++existing_name_count;
            existing_name_offset = offset;
        }
    }
    if (function_definition_count != 1) {
        return OpNameResult::InvalidEntryPoint;
    }
    if (existing_name_count > 1) {
        return OpNameResult::DuplicateEntryPointName;
    }

    auto encoded_name = detail::EncodeLiteralString(diagnostic_name);
    if (encoded_name.size() > std::numeric_limits<std::uint16_t>::max() - 2) {
        return OpNameResult::InvalidName;
    }
    std::vector<std::uint32_t> replacement;
    const std::size_t replace_offset = existing_name_count == 1 ? existing_name_offset
                                                                : insertion_offset;
    const std::size_t replace_count = existing_name_count == 1
                                          ? static_cast<std::size_t>(module[existing_name_offset] >> 16)
                                          : 0;
    replacement.reserve(module.size() - replace_count + 2 + encoded_name.size());
    replacement.insert(replacement.end(), module.begin(), module.begin() + replace_offset);
    replacement.push_back((static_cast<std::uint32_t>(2 + encoded_name.size()) << 16) |
                          detail::OpName);
    replacement.push_back(function_id);
    replacement.insert(replacement.end(), encoded_name.begin(), encoded_name.end());
    replacement.insert(replacement.end(), module.begin() + replace_offset + replace_count,
                       module.end());
    module.swap(replacement);
    return OpNameResult::Success;
}

} // namespace Vulkan::ShaderDiagnostic
