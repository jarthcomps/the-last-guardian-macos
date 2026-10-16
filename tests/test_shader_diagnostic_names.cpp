// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_vulkan/shader_diagnostic_names.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures{};
int malformed_case{};

void SetDiagnosticEnvironment(const char* value) {
#ifdef _WIN32
    _putenv_s("SHADPS4_SHADER_DIAGNOSTIC_NAMES", value == nullptr ? "" : value);
#else
    if (value == nullptr) {
        unsetenv("SHADPS4_SHADER_DIAGNOSTIC_NAMES");
    } else {
        setenv("SHADPS4_SHADER_DIAGNOSTIC_NAMES", value, 1);
    }
#endif
}

#define CHECK(condition)                                                                          \
    do {                                                                                          \
        if (!(condition)) {                                                                      \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,             \
                         #condition);                                                             \
            ++failures;                                                                           \
        }                                                                                         \
    } while (false)

std::vector<std::uint32_t> Encode(std::string_view text) {
    std::vector<std::uint32_t> words((text.size() + 1 + 3) / 4, 0);
    for (std::size_t i = 0; i < text.size(); ++i) {
        words[i / 4] |= static_cast<std::uint32_t>(static_cast<unsigned char>(text[i]))
                        << ((i % 4) * 8);
    }
    return words;
}

void AddInstruction(std::vector<std::uint32_t>& module, std::uint16_t opcode,
                    std::initializer_list<std::uint32_t> operands) {
    module.push_back((static_cast<std::uint32_t>(operands.size() + 1) << 16) | opcode);
    module.insert(module.end(), operands.begin(), operands.end());
}

void AddStringInstruction(std::vector<std::uint32_t>& module, std::uint16_t opcode,
                          std::uint32_t target, std::string_view text) {
    const auto encoded = Encode(text);
    module.push_back((static_cast<std::uint32_t>(encoded.size() + 2) << 16) | opcode);
    module.push_back(target);
    module.insert(module.end(), encoded.begin(), encoded.end());
}

std::vector<std::uint32_t> MakeModule(bool existing_name = false) {
    std::vector<std::uint32_t> module{0x07230203, 0x00010300, 0, 10, 0};
    AddInstruction(module, 17, {1});                         // OpCapability Shader
    AddInstruction(module, 14, {0, 1});                     // OpMemoryModel
    AddInstruction(module, 15, {5, 3, 0x6e69616d, 0, 4});   // OpEntryPoint ... "main" %4
    AddInstruction(module, 16, {3, 17, 1, 1, 1});            // OpExecutionMode LocalSize
    if (existing_name) {
        AddStringInstruction(module, 5, 3, "old_name");      // OpName %3 "old_name"
    }
    AddInstruction(module, 71, {4, 11, 0});                  // OpDecorate
    AddInstruction(module, 54, {1, 3, 0, 2});                // OpFunction %3
    AddInstruction(module, 56, {});                          // OpFunctionEnd
    return module;
}

bool HasEntryPointMain(const std::vector<std::uint32_t>& module) {
    for (std::size_t offset = 5; offset < module.size();) {
        const auto count = module[offset] >> 16;
        const auto opcode = module[offset] & 0xffff;
        if (count == 0 || count > module.size() - offset) {
            return false;
        }
        if (opcode == 15) {
            Vulkan::ShaderDiagnostic::detail::LiteralString name{};
            const auto decoded = Vulkan::ShaderDiagnostic::detail::DecodeLiteralString(
                std::span<const std::uint32_t>{module}.subspan(offset + 3, count - 3), false, name);
            return decoded && name.value == "main" && module[offset + 2] == 3;
        }
        offset += count;
    }
    return false;
}

std::size_t CountTargetNames(const std::vector<std::uint32_t>& module, std::uint32_t target,
                             std::string_view* found_name = nullptr) {
    std::size_t found{};
    for (std::size_t offset = 5; offset < module.size();) {
        const auto count = module[offset] >> 16;
        const auto opcode = module[offset] & 0xffff;
        if (count == 0 || count > module.size() - offset) {
            return 0;
        }
        if (opcode == 5 && module[offset + 1] == target) {
            ++found;
            Vulkan::ShaderDiagnostic::detail::LiteralString name{};
            const auto decoded = Vulkan::ShaderDiagnostic::detail::DecodeLiteralString(
                std::span<const std::uint32_t>{module}.subspan(offset + 2, count - 2), true, name);
            if (decoded && found_name != nullptr) {
                // Keep a view into stable test-owned storage for the duration of the assertion.
                static thread_local std::string storage;
                storage = std::move(name.value);
                *found_name = storage;
            }
        }
        offset += count;
    }
    return found;
}

void CheckUnchangedOnFailure(std::vector<std::uint32_t> module,
                             Vulkan::ShaderDiagnostic::OpNameResult expected,
                             std::string_view name = "shadps4_guest_Compute_0123456789abcdef_p0") {
    ++malformed_case;
    const auto before = module;
    const auto actual = Vulkan::ShaderDiagnostic::SetEntryPointOpName(module, name);
    if (actual != expected) {
        std::fprintf(stderr, "malformed case %d: expected OpNameResult %d got %d\n",
                     malformed_case,
                     static_cast<int>(expected), static_cast<int>(actual));
    }
    CHECK(actual == expected);
    CHECK(module == before);
}

void TestGeneratedNames() {
    using namespace Vulkan::ShaderDiagnostic;
    SetDiagnosticEnvironment(nullptr);
    CHECK(!NamesEnabled());
    SetDiagnosticEnvironment("true");
    CHECK(!NamesEnabled());
    SetDiagnosticEnvironment("1");
    CHECK(NamesEnabled());
    SetDiagnosticEnvironment(nullptr);
    CHECK(GuestName("Compute", 0x0123456789abcdef, 7) ==
          "shadps4_guest_Compute_0123456789abcdef_p7");
    CHECK(HostTilingName("Thin1DThin", 32, false, true) ==
          "shadps4_host_tiling_Thin1DThin_32_detiler_single");
    CHECK(IsDiagnosticName("shadps4_guest_Fragment_0000000000000000_p0"));
    CHECK(IsDiagnosticName("shadps4_host_tiling_Depth2DThin64_128_tiler_generic"));
    CHECK(!IsDiagnosticName("shadps4_guest_Compute_123_p0"));
    CHECK(!IsDiagnosticName("shadps4_host_tiling_mode_7_tiler_generic"));
    CHECK(!IsDiagnosticName("shadps4_guest_Compute_0123456789ABCDEF_p0"));
    CHECK(GuestName("Unknown", 1, 0).empty());
    CHECK(HostTilingName("bad_mode", 32, true, false).empty());

    constexpr std::string_view host_prefix = "shadps4_host_tiling_";
    constexpr std::string_view host_suffix = "_32_tiler_generic";
    const std::string max_mode(127 - host_prefix.size() - host_suffix.size(), 'A');
    const auto max_length_name = HostTilingName(max_mode, 32, true, false);
    CHECK(max_length_name.size() == 127);
    CHECK(IsDiagnosticName(max_length_name));
    CHECK(HostTilingName(max_mode + "A", 32, true, false).empty());
    CHECK(!IsDiagnosticName(std::string(host_prefix) + max_mode + "A" + std::string(host_suffix)));
}

void TestDiagnosticCacheRoot() {
    using Vulkan::ShaderDiagnostic::CacheRoot;
    const std::filesystem::path original_root{"cache/root"};
    const auto normal_root = CacheRoot(original_root, false);
    const auto diagnostic_root = CacheRoot(original_root, true);
    CHECK(normal_root == original_root);
    CHECK(diagnostic_root == original_root / "shader-diagnostics");

    const std::filesystem::path title{"CUSA00001"};
    const auto normal_title_folder = normal_root / title;
    auto archived_title = title;
    archived_title.replace_extension(".zip");
    const auto diagnostic_title_folder = diagnostic_root / title;
    const auto diagnostic_title_archive = diagnostic_root / archived_title;
    CHECK(normal_title_folder.parent_path() == original_root);
    CHECK(diagnostic_title_folder.parent_path() == diagnostic_root);
    CHECK(diagnostic_title_archive.parent_path() == diagnostic_root);
    CHECK(diagnostic_title_archive.filename() == "CUSA00001.zip");
}

void TestInsertAndRewrite() {
    using namespace Vulkan::ShaderDiagnostic;
    auto module = MakeModule();
    const auto name = GuestName("Compute", 0x0123456789abcdef, 0);
    CHECK(SetEntryPointOpName(module, name) == OpNameResult::Success);
    CHECK(HasEntryPointMain(module));
    std::string_view found_name;
    CHECK(CountTargetNames(module, 3, &found_name) == 1);
    CHECK(found_name == name);

    const auto replacement_name = HostTilingName("Thin1DThin", 32, false, false);
    CHECK(SetEntryPointOpName(module, replacement_name) == OpNameResult::Success);
    CHECK(HasEntryPointMain(module));
    CHECK(CountTargetNames(module, 3, &found_name) == 1);
    CHECK(found_name == replacement_name);

    auto existing = MakeModule(true);
    CHECK(SetEntryPointOpName(existing, name) == OpNameResult::Success);
    CHECK(HasEntryPointMain(existing));
    CHECK(CountTargetNames(existing, 3, &found_name) == 1);
    CHECK(found_name == name);
}

void TestRejectsMalformedInputWithoutMutation() {
    using namespace Vulkan::ShaderDiagnostic;
    constexpr char embedded_nul[] = "shadps4_guest_Compute_0123456789abcdef_p0\0evil";
    CheckUnchangedOnFailure(MakeModule(), OpNameResult::InvalidName,
                            std::string_view{embedded_nul, sizeof(embedded_nul) - 1});

    auto invalid_header = MakeModule();
    invalid_header[0] = 0;
    CheckUnchangedOnFailure(std::move(invalid_header), OpNameResult::InvalidHeader);

    const std::string overlong_mode =
        std::string{"shadps4_host_tiling_"} + std::string(100, 'M') + "_32_tiler_generic";
    CheckUnchangedOnFailure(MakeModule(), OpNameResult::InvalidName, overlong_mode);
    const std::string overlong_permutation =
        "shadps4_guest_Compute_0123456789abcdef_p" + std::string(100, '9');
    CheckUnchangedOnFailure(MakeModule(), OpNameResult::InvalidName, overlong_permutation);

    auto zero_word_count = MakeModule();
    zero_word_count[5] = 17;
    CheckUnchangedOnFailure(std::move(zero_word_count), OpNameResult::MalformedInstruction);

    auto truncated = MakeModule();
    truncated[5] = (0xffffu << 16) | 17u;
    CheckUnchangedOnFailure(std::move(truncated), OpNameResult::MalformedInstruction);

    auto unterminated_entry = MakeModule();
    // Remove the interface ID so the final entry-point name word has no null terminator.
    unterminated_entry.erase(unterminated_entry.begin() + 14,
                             unterminated_entry.begin() + 16);
    unterminated_entry[10] = (4u << 16) | 15u;
    CheckUnchangedOnFailure(std::move(unterminated_entry), OpNameResult::MalformedString);

    auto invalid_entry_padding = MakeModule();
    invalid_entry_padding[13] = 0x78696100;
    CheckUnchangedOnFailure(std::move(invalid_entry_padding), OpNameResult::MalformedString);

    auto ambiguous = MakeModule();
    AddInstruction(ambiguous, 15, {5, 5, 0x6e69616d, 0});
    AddInstruction(ambiguous, 54, {1, 5, 0, 2});
    CheckUnchangedOnFailure(std::move(ambiguous), OpNameResult::AmbiguousEntryPoint);

    auto duplicate_name = MakeModule(true);
    AddStringInstruction(duplicate_name, 5, 3, "duplicate");
    CheckUnchangedOnFailure(std::move(duplicate_name), OpNameResult::DuplicateEntryPointName);

    auto malformed_name = MakeModule();
    AddInstruction(malformed_name, 5, {3});
    CheckUnchangedOnFailure(std::move(malformed_name), OpNameResult::MalformedString);

    auto invalid_utf8_name = MakeModule();
    AddInstruction(invalid_utf8_name, 5, {3, 0x0080c000});
    CheckUnchangedOnFailure(std::move(invalid_utf8_name), OpNameResult::MalformedString);

    auto missing_function = MakeModule();
    for (std::size_t offset = 5; offset < missing_function.size();) {
        const auto count = static_cast<std::size_t>(missing_function[offset] >> 16);
        if ((missing_function[offset] & 0xffff) == 54) {
            missing_function.erase(missing_function.begin() + offset,
                                   missing_function.begin() + offset + count);
            break;
        }
        offset += count;
    }
    CheckUnchangedOnFailure(std::move(missing_function), OpNameResult::InvalidEntryPoint);
}

} // namespace

int main() {
    TestGeneratedNames();
    TestDiagnosticCacheRoot();
    TestInsertAndRewrite();
    TestRejectsMalformedInputWithoutMutation();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
