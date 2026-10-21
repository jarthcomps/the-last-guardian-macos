// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>
#include <variant>
#include <tsl/robin_map.h>
#include <tsl/robin_set.h>
#include "common/assert.h"
#include "shader_recompiler/profile.h"
#include "shader_recompiler/recompiler.h"
#include "shader_recompiler/specialization.h"
#include "video_core/renderer_vulkan/vk_compute_pipeline.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_pipeline_memo.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"

template <>
struct std::hash<vk::ShaderModule> {
    std::size_t operator()(const vk::ShaderModule& module) const noexcept {
        return std::hash<size_t>{}(reinterpret_cast<size_t>((VkShaderModule)module));
    }
};

namespace AmdGpu {
class Liverpool;
}

namespace Serialization {
struct Archive;
}

namespace Shader {
struct Info;
}

namespace Vulkan {

class Instance;
class Scheduler;
class ShaderCache;

struct ComputeCaptureModules {
    u64 guest_hash{};
    u64 program_hash{};
    size_t permutation{};
    std::span<const u32> original_spv;
    std::span<const u32> active_spv;
    std::string_view replacement_id;
    u32 replacement_version{};
    u64 replacement_cache_identity{};
    bool subgroup_size_64_supported{};
    u32 profile_subgroup_size{};
    u32 required_subgroup_size{};
};

struct Program {
    struct Module {
        struct CaptureStorage {
            u64 guest_hash{};
            u64 program_hash{};
            size_t permutation{};
            std::vector<u32> original_spv;
            std::vector<u32> active_spv;
            std::string_view replacement_id;
            u32 replacement_version{};
            u64 replacement_cache_identity{};
        };

        vk::ShaderModule module;
        Shader::StageSpecialization spec;
        CaptureStorage capture;
    };
    static constexpr size_t MaxPermutations = 8;
    using ModuleList = boost::container::small_vector<Module, MaxPermutations>;

    Shader::Info info;
    ModuleList modules{};
    size_t last_perm{~size_t{0}}; ///< Variant picked by the last lookup.

    Program() = default;
    Program(Shader::HwStage stage, Shader::SwStage l_stage, Shader::ShaderParams params)
        : info{stage, l_stage, params} {}

    void AddPermut(vk::ShaderModule module, Shader::StageSpecialization&& spec) {
        AddPermut(module, std::move(spec), Module::CaptureStorage{});
    }

    void AddPermut(vk::ShaderModule module, Shader::StageSpecialization&& spec,
                   Module::CaptureStorage capture) {
        modules.emplace_back(module, std::move(spec), std::move(capture));
    }

    void InsertPermut(vk::ShaderModule module, Shader::StageSpecialization&& spec,
                      size_t perm_idx) {
        InsertPermut(module, std::move(spec), perm_idx, Module::CaptureStorage{});
    }

    void InsertPermut(vk::ShaderModule module, Shader::StageSpecialization&& spec,
                      size_t perm_idx, Module::CaptureStorage capture) {
        modules.resize(std::max(modules.size(), perm_idx + 1)); // <-- beware of realloc
        modules[perm_idx] = {module, std::move(spec), std::move(capture)};
    }
};

struct DrawIndirectParams {
    u16 vertex_sgpr_offset;
    u32 instance_sgpr_offset;
};

class PipelineCache {
public:
    explicit PipelineCache(const Instance& instance, Scheduler& scheduler,
                           AmdGpu::Liverpool* liverpool);
    ~PipelineCache();

    void WarmUp();
    void Sync();

    bool LoadComputePipeline(Serialization::Archive& ar);
    bool LoadGraphicsPipeline(Serialization::Archive& ar);
    bool LoadPipelineStage(Serialization::Archive& ar, size_t stage);

    const GraphicsPipeline* GetGraphicsPipeline(const DrawIndirectParams params = {});

    const ComputePipeline* GetComputePipeline();

    /// Returns the original and active SPIR-V for a captured compute target. The spans point into
    /// immutable Program storage and remain valid until the PipelineCache is destroyed.
    std::optional<ComputeCaptureModules> GetActiveComputeCaptureModules() const;

    /// shader_backing_reads: shader headers and fetch shader code are read through the physical
    /// backing when `is_cpu_copy_current(address, size)` is true for the bytes read.
    /// With `verify`, backing reads are compared with the guest mapping and not used.
    void SetBackingReadCheck(std::function<bool(VAddr, u64)> is_cpu_copy_current,
                             bool verify = false);

    /// The fetch shader is returned by pointer into the program's variant list. It is valid only
    /// until the next lookup, which may add variants: read it right away.
    using Result = std::tuple<const Shader::Info*, vk::ShaderModule,
                              const std::optional<Shader::Gcn::FetchShaderData>*, u64>;
    Result GetProgram(Shader::HwStage stage, Shader::SwStage l_stage,
                      const Shader::ShaderParams& params, Shader::Backend::Bindings& binding);

    std::optional<vk::ShaderModule> ReplaceShader(vk::ShaderModule module,
                                                  std::span<const u32> spv_code);

    static std::string GetShaderName(Shader::HwStage stage, u64 hash,
                                     std::optional<size_t> perm = {});

    auto& GetProfile() const {
        return profile;
    }

private:
    bool RefreshGraphicsKey();
    bool RefreshGraphicsStages();
    bool RefreshComputeKey();

    /// GetProgram for a hash that program_cache does not hold yet: creates the program and its
    /// first variant. Kept out of line so the lookup of known programs stays small.
    SHAD_NO_INLINE Result CreateProgram(Shader::HwStage hw_stage, Shader::SwStage sw_stage,
                                        const Shader::ShaderParams& params,
                                        Shader::Backend::Bindings& binding,
                                        const Shader::RuntimeInfo& runtime_info,
                                        std::unique_ptr<Program>& slot);
    /// GetProgram for a known program when its last variant does not match: finds or compiles the
    /// variant for the stage's current state. `fast_perm` is the variant the fast check picked.
    SHAD_NO_INLINE Result SelectVariant(Shader::HwStage hw_stage, Shader::SwStage sw_stage,
                                        const Shader::ShaderParams& params,
                                        Shader::Backend::Bindings& binding, Program& program,
                                        const Shader::RuntimeInfo& runtime_info, size_t fast_perm);

    void DumpShader(std::span<const u32> code, u64 hash, Shader::HwStage stage, size_t perm_idx,
                    std::string_view ext);
    std::optional<std::vector<u32>> GetShaderPatch(u64 hash, Shader::HwStage stage, size_t perm_idx,
                                                   std::string_view ext);
    vk::ShaderModule CompileModule(Shader::Info& info, Shader::RuntimeInfo& runtime_info,
                                   const std::span<const u32>& code, size_t perm_idx,
                                   Shader::Backend::Bindings& binding,
                                   Program::Module::CaptureStorage* capture = nullptr);
    const Shader::RuntimeInfo& BuildRuntimeInfo(Shader::HwStage stage, Shader::SwStage l_stage);

    [[nodiscard]] bool IsPipelineCacheDirty() const {
        return num_new_pipelines > 0;
    }

private:
    const Instance& instance;
    Scheduler& scheduler;
    AmdGpu::Liverpool* liverpool;
    DescriptorHeap desc_heap;
    vk::UniquePipelineCache pipeline_cache;
    vk::UniquePipelineLayout pipeline_layout;
    Shader::Profile profile{};
    Shader::Pools pools;
    DrawIndirectParams draw_indirect_params{};
    tsl::robin_map<size_t, std::unique_ptr<Program>> program_cache;
    tsl::robin_map<ComputePipelineKey, std::unique_ptr<ComputePipeline>> compute_pipelines;
    tsl::robin_map<GraphicsPipelineKey, std::unique_ptr<GraphicsPipeline>> graphics_pipelines;
    std::array<Shader::RuntimeInfo, MaxShaderStages> runtime_infos{};
    std::array<const Shader::Info*, MaxShaderStages> infos{};
    std::array<vk::ShaderModule, MaxShaderStages> modules{};
    const Program::Module* active_compute_module{};
    std::optional<Shader::Gcn::FetchShaderData> fetch_shader{};
    GraphicsPipelineKey graphics_key{};
    ComputePipelineKey compute_key{};
    /// Program each software stage found last, by hash. program_cache never removes entries, so
    /// the remembered program stays valid.
    std::array<PipelineLookupMemo<size_t, Program>, MaxShaderStages> program_memo{};
    /// Pipeline the last graphics or compute lookup found, with its key. ReplaceShader, the only
    /// place that erases pipelines, invalidates both.
    PipelineLookupMemo<GraphicsPipelineKey, const GraphicsPipeline> last_graphics_lookup;
    PipelineLookupMemo<ComputePipelineKey, const ComputePipeline> last_compute_lookup;
    u32 num_new_pipelines{};      // new pipelines added to the cache since the game start
    const u32 stage_lookup_reuse; // 0 = off, 1 = check the last variant first, 2 = verify
    /// shader_backing_reads: true if the CPU copy of guest memory bytes is current.
    std::function<bool(VAddr, u64)> cpu_copy_is_current;
    bool backing_verify{}; ///< shader_backing_verify
    tsl::robin_set<u64> logged_stage_mismatches;

    // Only if Config::collectShadersForDebug()
    tsl::robin_map<vk::ShaderModule,
                   std::vector<std::variant<GraphicsPipelineKey, ComputePipelineKey>>>
        module_related_pipelines;
};

} // namespace Vulkan
