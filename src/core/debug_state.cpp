// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fstream>
#include <imgui.h>

#include "common/assert.h"
#include "common/native_clock.h"
#include "common/path_util.h"
#include "common/singleton.h"
#include "core/signals.h"
#include "debug_state.h"
#include "devtools/widget/common.h"
#include "libraries/kernel/time.h"
#include "libraries/system/msgdialog.h"
#include "video_core/amdgpu/pm4_cmds.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"

using namespace DebugStateType;

DebugStateImpl& DebugState = *Common::Singleton<DebugStateImpl>::Instance();

bool DebugStateType::showing_debug_menu_bar = false;

static ThreadID ThisThreadID() {
#ifdef _WIN32
    return GetCurrentThreadId();
#else
    return pthread_self();
#endif
}

static void PauseThread(ThreadID id) {
#ifdef _WIN32
    auto handle = OpenThread(THREAD_SUSPEND_RESUME, FALSE, id);
    SuspendThread(handle);
    CloseHandle(handle);
#else
    pthread_kill(id, SIGSLEEP);
#endif
}

static void ResumeThread(ThreadID id) {
#ifdef _WIN32
    auto handle = OpenThread(THREAD_SUSPEND_RESUME, FALSE, id);
    ResumeThread(handle);
    CloseHandle(handle);
#else
    pthread_kill(id, SIGSLEEP);
#endif
}

void DebugStateImpl::AddCurrentThreadToGuestList() {
    std::lock_guard lock{guest_threads_mutex};
    const ThreadID id = ThisThreadID();
    guest_threads.push_back(id);
}

void DebugStateImpl::RemoveCurrentThreadFromGuestList() {
    std::lock_guard lock{guest_threads_mutex};
    const ThreadID id = ThisThreadID();
    std::erase_if(guest_threads, [&](const ThreadID& v) { return v == id; });
}

void DebugStateImpl::PauseGuestThreads() {
    using namespace Libraries::MsgDialog;
    std::unique_lock lock{guest_threads_mutex};
    if (is_guest_threads_paused) {
        return;
    }
    if (ShouldPauseInSubmit()) {
        waiting_submit_pause = false;
        should_show_frame_dump = true;
    }
    bool self_guest = false;
    ThreadID self_id = ThisThreadID();
    for (const auto& id : guest_threads) {
        if (id == self_id) {
            self_guest = true;
        } else {
            PauseThread(id);
        }
    }
    pause_time = Libraries::Kernel::Dev::GetClock()->GetUptime();
    is_guest_threads_paused = true;
    lock.unlock();
    if (self_guest) {
        PauseThread(self_id);
    }
}

void DebugStateImpl::ResumeGuestThreads() {
    std::lock_guard lock{guest_threads_mutex};
    if (!is_guest_threads_paused) {
        return;
    }

    u64 delta_time = Libraries::Kernel::Dev::GetClock()->GetUptime() - pause_time;
    Libraries::Kernel::Dev::GetInitialPtc() += delta_time;
    for (const auto& id : guest_threads) {
        ResumeThread(id);
    }
    is_guest_threads_paused = false;
}

void DebugStateImpl::RequestFrameDump(s32 count) {
    ASSERT(!DumpingCurrentFrame());
    gnm_frame_dump_request_count = count;
    frame_dump_list.clear();
    frame_dump_list.resize(count);
    const auto f = gnm_frame_count.load() + 1;
    for (size_t i = 0; i < count; ++i) {
        frame_dump_list[i].frame_id = f + i;
    }
    waiting_submit_pause = true;
}

void DebugStateImpl::PushQueueDump(QueueDump dump) {
    ASSERT(DumpingCurrentFrame());
    std::unique_lock lock{frame_dump_list_mutex};
    auto& frame = GetFrameDump();
    { // Find draw calls
        auto data = std::span{dump.data};
        auto initial_data = data.data();
        while (!data.empty()) {
            const auto* header = reinterpret_cast<const AmdGpu::PM4Type3Header*>(data.data());
            const auto type = header->type;
            if (type == 2) {
                data = data.subspan(1);
            } else if (type != 3) {
                UNREACHABLE();
            }
            const AmdGpu::PM4ItOpcode opcode = header->opcode;
            if (Core::Devtools::Widget::IsDrawCall(opcode)) {
                const auto offset =
                    reinterpret_cast<uintptr_t>(header) - reinterpret_cast<uintptr_t>(initial_data);
                const auto addr = dump.base_addr + offset;
                waiting_reg_dumps.emplace(addr, &frame);
                waiting_reg_dumps_dbg.emplace(
                    addr,
                    fmt::format("#{} h({}) queue {} {} {}",
                                frame_dump_list.size() - gnm_frame_dump_request_count, addr,
                                magic_enum::enum_name(dump.type), dump.submit_num, dump.num2));
            }
            data = data.subspan(header->NumWords() + 1);
        }
    }
    frame.queues.push_back(std::move(dump));
}

std::optional<RegDump*> DebugStateImpl::GetRegDump(uintptr_t base_addr, uintptr_t header_addr) {
    const auto it = waiting_reg_dumps.find(header_addr);
    if (it == waiting_reg_dumps.end()) {
        return std::nullopt;
    }
    auto& frame = *it->second;
    waiting_reg_dumps.erase(it);
    waiting_reg_dumps_dbg.erase(waiting_reg_dumps_dbg.find(header_addr));
    return &frame.regs[header_addr - base_addr];
}

void DebugStateImpl::PushRegsDump(uintptr_t base_addr, uintptr_t header_addr,
                                  const AmdGpu::Regs& regs) {
    std::scoped_lock lock{frame_dump_list_mutex};

    auto dump = GetRegDump(base_addr, header_addr);
    if (!dump) {
        return;
    }

    (*dump)->regs = regs;

    for (int i = 0; i < RegDump::MaxShaderStages; i++) {
        if ((*dump)->regs.stage_enable.IsStageEnabled(i)) {
            auto stage = (*dump)->regs.ProgramForStage(i);
            if (stage->address) {
                const auto params = AmdGpu::GetParams(*stage);
                (*dump)->stages[i] = PipelineShaderProgramDump{
                    .name = Vulkan::PipelineCache::GetShaderName(Shader::StageFromIndex(i),
                                                                 params.hash),
                    .hash = params.hash,
                    .user_data = *stage,
                    .code = std::vector<u32>{params.code.begin(), params.code.end()},
                };
            }
        }
    }
}

void DebugStateImpl::PushRegsDumpCompute(uintptr_t base_addr, uintptr_t header_addr,
                                         const CsState& cs_state) {
    std::scoped_lock lock{frame_dump_list_mutex};

    auto dump = GetRegDump(base_addr, header_addr);
    if (!dump) {
        return;
    }

    (*dump)->is_compute = true;
    auto& cs = (*dump)->regs.cs_program;
    cs = cs_state;

    const auto params = AmdGpu::GetParams(cs);
    (*dump)->cs_data = PipelineComputerProgramDump{
        .name = Vulkan::PipelineCache::GetShaderName(Shader::HwStage::Compute, params.hash),
        .hash = params.hash,
        .cs_program = cs,
        .code = std::vector<u32>{params.code.begin(), params.code.end()},
    };
}

void DebugStateImpl::ExportFrameDump(const FrameDump& frame) const {
    const auto path = Common::FS::GetUserPath(Common::FS::PathType::LogDir) /
                      fmt::format("frame_dump_{}.csv", frame.frame_id);
    std::ofstream out{path};
    if (!out) {
        return;
    }
    out << "idx,queue,submit,op,pass,color_targets,depth,rt_addr,rt_w,rt_h,depth_addr,depth_w,"
           "depth_h,indices,instances,groups_x,groups_y,groups_z,hash0,hash1,hash2,hash3,hash4,"
           "cs_hash"
        << '\n';
    u32 idx = 0;
    u32 pass = 0;
    u64 prev_key = ~0ULL;
    for (const auto& queue : frame.queues) {
        auto data = std::span{queue.data};
        const auto* initial_data = data.data();
        while (!data.empty()) {
            const auto* header = reinterpret_cast<const AmdGpu::PM4Type3Header*>(data.data());
            if (header->type == 2) {
                data = data.subspan(1);
                continue;
            }
            if (header->type != 3) {
                break;
            }
            const AmdGpu::PM4ItOpcode opcode = header->opcode;
            if (Core::Devtools::Widget::IsDrawCall(opcode)) {
                const auto offset =
                    reinterpret_cast<uintptr_t>(header) - reinterpret_cast<uintptr_t>(initial_data);
                const auto it = frame.regs.find(offset);
                const auto op_name = magic_enum::enum_name(opcode);
                if (it == frame.regs.end()) {
                    out << fmt::format("{},{},{},{}\n", idx++, magic_enum::enum_name(queue.type),
                                       queue.submit_num, op_name);
                } else {
                    const RegDump& dump = it->second;
                    const auto& regs = dump.regs;
                    u32 color_targets = 0;
                    u64 rt_addr = 0;
                    u32 rt_w = 0;
                    u32 rt_h = 0;
                    u64 depth_addr = 0;
                    u32 depth_w = 0;
                    u32 depth_h = 0;
                    bool depth = false;
                    if (!dump.is_compute) {
                        for (u32 i = 0; i < AmdGpu::NUM_COLOR_BUFFERS; ++i) {
                            const auto& cb = regs.color_buffers[i];
                            if (cb && regs.color_target_mask.GetMask(i) != 0) {
                                if (color_targets++ == 0) {
                                    rt_addr = cb.Address();
                                    rt_w = cb.Pitch();
                                    rt_h = cb.Height();
                                }
                            }
                        }
                        depth = regs.depth_buffer.DepthValid();
                        if (depth) {
                            depth_addr = regs.depth_buffer.DepthAddress();
                            depth_w = regs.depth_buffer.Pitch();
                            depth_h = regs.depth_buffer.Height();
                        }
                        const u64 key = rt_addr * 31 + depth_addr * 17 + color_targets;
                        if (key != prev_key) {
                            prev_key = key;
                            ++pass;
                        }
                    }
                    out << fmt::format(
                        "{},{},{},{},{},{},{},{:#x},{},{},{:#x},{},{},{},{},{},{},{}", idx++,
                        magic_enum::enum_name(queue.type), queue.submit_num, op_name,
                        dump.is_compute ? 0 : pass, color_targets, depth ? 1 : 0, rt_addr, rt_w,
                        rt_h, depth_addr, depth_w, depth_h, dump.is_compute ? 0 : regs.num_indices,
                        dump.is_compute ? 0 : regs.num_instances.NumInstances(),
                        dump.is_compute ? dump.regs.cs_program.dim_x : 0,
                        dump.is_compute ? dump.regs.cs_program.dim_y : 0,
                        dump.is_compute ? dump.regs.cs_program.dim_z : 0);
                    for (const auto& stage : dump.stages) {
                        out << fmt::format(",{:#x}", stage.hash);
                    }
                    out << fmt::format(",{:#x}\n", dump.is_compute ? dump.cs_data.hash : 0);
                }
            }
            data = data.subspan(header->NumWords() + 1);
        }
    }
}

void DebugStateImpl::CollectShader(const std::string& name, Shader::SwStage l_stage,
                                   vk::ShaderModule module, std::span<const u32> spv,
                                   std::span<const u32> raw_code, std::span<const u32> patch_spv,
                                   bool is_patched) {
    shader_dump_list.emplace_back(name, l_stage, module, std::vector<u32>{spv.begin(), spv.end()},
                                  std::vector<u32>{raw_code.begin(), raw_code.end()},
                                  std::vector<u32>{patch_spv.begin(), patch_spv.end()}, is_patched);
}
