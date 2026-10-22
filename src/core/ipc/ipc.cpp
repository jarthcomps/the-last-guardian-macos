//  SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#include "ipc.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <fmt/format.h>

#include <SDL3/SDL.h>

#include "common/singleton.h"
#include "common/memory_patcher.h"
#include "common/thread.h"
#include "common/types.h"
#include "core/debug_state.h"
#include "core/debugger.h"
#include "core/emulator_settings.h"
#include "core/emulator_state.h"
#include "core/libraries/audio/audioout.h"
#include "core/libraries/pad/pad.h"
#include "input/input_handler.h"
#include "input/controller.h"
#include "sdl_window.h"
#include "src/core/libraries/usbd/usbd.h"
#include "video_core/readback_stats.h"
#include "video_core/renderdoc.h"
#include "video_core/renderer_vulkan/vk_presenter.h"
#include "test_control.h"

extern std::unique_ptr<Vulkan::Presenter> presenter;

namespace {

namespace TestControl = IPCTestControl;

bool test_control_enabled = false;

Libraries::Pad::OrbisPadButtonDataOffset ToOrbisButton(TestControl::Button button) {
    using OrbisButton = Libraries::Pad::OrbisPadButtonDataOffset;
    switch (button) {
    case TestControl::Button::Cross:
        return OrbisButton::Cross;
    case TestControl::Button::Circle:
        return OrbisButton::Circle;
    case TestControl::Button::Square:
        return OrbisButton::Square;
    case TestControl::Button::Triangle:
        return OrbisButton::Triangle;
    case TestControl::Button::Options:
        return OrbisButton::Options;
    case TestControl::Button::Up:
        return OrbisButton::Up;
    case TestControl::Button::Down:
        return OrbisButton::Down;
    case TestControl::Button::Left:
        return OrbisButton::Left;
    case TestControl::Button::Right:
        return OrbisButton::Right;
    case TestControl::Button::L1:
        return OrbisButton::L1;
    case TestControl::Button::R1:
        return OrbisButton::R1;
    case TestControl::Button::L2:
        return OrbisButton::L2;
    case TestControl::Button::R2:
        return OrbisButton::R2;
    case TestControl::Button::L3:
        return OrbisButton::L3;
    case TestControl::Button::R3:
        return OrbisButton::R3;
    case TestControl::Button::TouchPad:
        return OrbisButton::TouchPad;
    case TestControl::Button::Connect:
        return OrbisButton::None;
    }
    return OrbisButton::None;
}

Input::Axis ToInputAxis(TestControl::Axis axis) {
    switch (axis) {
    case TestControl::Axis::LeftX:
        return Input::Axis::LeftX;
    case TestControl::Axis::LeftY:
        return Input::Axis::LeftY;
    case TestControl::Axis::RightX:
        return Input::Axis::RightX;
    case TestControl::Axis::RightY:
        return Input::Axis::RightY;
    }
    return Input::Axis::LeftX;
}

} // namespace

/**
 * Protocol summary:
 * - IPC is enabled by setting the SHADPS4_ENABLE_IPC environment variable to "true"
 * - Input will be stdin & output stderr
 * - Strings are sent as UTF8
 * - Each communication line is terminated by a newline character ('\n')
 * - Each command parameter will be separated by a newline character ('\n'),
 *   variadic commands will start sending the number of parameters after the cmd word.
 *   Any ('\n') in the parameter must be escaped by a backslash ('\\n')
 * - Numbers can be sent with any base. Must prefix the number with '0x' for hex,
 *   '0b' for binary, or '0' for octal. Decimal numbers
 *   will be sent without any prefix.
 * - Output will be started by (';')
 * - The IPC server(this) will send a block started by
 *   #IPC_ENABLED
 *   and ended by
 *   #IPC_END
 *   In between, it will send the current capabilities and commands before the emulator start
 * - The IPC client(e.g., launcher) will send RUN then START to continue the execution
 **/

/**
 * Command list:
 * - CAPABILITIES:
 *   - ENABLE_MEMORY_PATCH: enables PATCH_MEMORY command
 *   - ENABLE_EMU_CONTROL: enables PAUSE, RESUME, STOP, TOGGLE_FULLSCREEN commands
 *   - ENABLE_TEST_CONTROL: emitted only when SHADPS4_ENABLE_TEST_CONTROL=1 and IPC is enabled
 * - INPUT CMD:
 *   - RUN: start the emulator execution
 *   - START: start the game execution
 *   - PATCH_MEMORY(
 *       modName: str, offset: str, value: str,
 *       target: str, size: str, isOffset: number, littleEndian: number,
 *       patchMask: number, maskOffset: number
 *     ): add a memory patch, check @ref MemoryPatcher::PatchMemory for details
 *   - PAUSE: pause the game execution
 *   - RESUME: resume the game execution
 *   - STOP: stop and quit the emulator
 *   - TOGGLE_FULLSCREEN: enable / disable fullscreen
 *   - TEST_PAD_BUTTON <slot 0..3> <button> <pressed 0|1>
 *   - TEST_PAD_AXIS <slot 0..3> <left_x|left_y|right_x|right_y> <value 0..255>
 *   - TEST_SCREENSHOT: request a game-only screenshot using the normal screenshot filename/path
 *   - TEST_METRICS: report cumulative present count, frame draw/dispatch averages, monotonic ns
 * - OUTPUT CMD:
 *   - RESTART(argn: number, argv: ...string): Request restart of the emulator, must call STOP
 **/

void IPC::Init() {
    const char* enabledEnv = std::getenv("SHADPS4_ENABLE_IPC");
    enabled = enabledEnv && strcmp(enabledEnv, "true") == 0;
    if (!enabled) {
        return;
    }
    const char* testControlEnv = std::getenv("SHADPS4_ENABLE_TEST_CONTROL");
    test_control_enabled = TestControl::Enabled(enabled, testControlEnv ? testControlEnv : "");

    EmulatorState::GetInstance()->SetAutoPatchesLoadEnabled(false);

    input_thread = std::jthread([this] {
        Common::SetCurrentThreadName("IPC Read thread");
        this->InputLoop();
    });

    std::cerr << ";#IPC_ENABLED\n";
    std::cerr << ";ENABLE_MEMORY_PATCH\n";
    std::cerr << ";ENABLE_EMU_CONTROL\n";
    if (test_control_enabled) {
        std::cerr << ";ENABLE_TEST_CONTROL\n";
    }
    std::cerr << ";#IPC_END\n";
    std::cerr.flush();

    const auto ok = run_semaphore.try_acquire_for(std::chrono::seconds(5));
    if (!ok) {
        std::cerr << "IPC: Failed to acquire run semaphore, closing process.\n";
        exit(1);
    }
}

void IPC::SendRestart(const std::vector<std::string>& args) {
    std::cerr << ";RESTART\n";
    std::cerr << ";" << args.size() << "\n";
    for (const auto& arg : args) {
        std::cerr << ";" << arg << "\n";
    }
    std::cerr.flush();
}

void IPC::InputLoop() {
    auto next_str = [&] -> const std::string& {
        static std::string line_buffer;
        do {
            std::getline(std::cin, line_buffer, '\n');
        } while (!line_buffer.empty() && line_buffer.back() == '\\');
        return line_buffer;
    };
    auto next_u64 = [&] -> u64 {
        auto& str = next_str();
        return std::stoull(str, nullptr, 0);
    };

    while (true) {
        std::string cmd;
        std::getline(std::cin, cmd);
        if (cmd.empty()) {
            continue;
        }
        if (cmd.starts_with("TEST_")) {
            using TestControl::ParseStatus;
            const auto status = TestControl::Dispatch(
                cmd, test_control_enabled,
                [](int slot, TestControl::Button button, bool pressed) {
                    auto& controllers = *Common::Singleton<Input::GameControllers>::Instance();
                    if (button == TestControl::Button::Connect) {
                        if (pressed) {
                            controllers[slot]->ConnectController(controllers[slot]->m_sdl_gamepad);
                        } else {
                            controllers[slot]->DisconnectController();
                        }
                        return;
                    }
                    controllers[slot]->Button(ToOrbisButton(button), pressed);
                },
                [](int slot, TestControl::Axis axis, int value) {
                    auto& controllers = *Common::Singleton<Input::GameControllers>::Instance();
                    controllers[slot]->Axis(ToInputAxis(axis), value, false);
                },
                [] { VideoCore::RequestScreenshot(VideoCore::ScreenshotRequest::GameOnly); },
                [] {
                    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
                    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed)
                                        .count();
                    std::cerr << fmt::format(";TEST_METRICS {} {:.1f} {:.1f} {}\n",
                                             VideoCore::ReadbackStats::FrameIndex(),
                                             DebugState.GetDrawCallsAvg(),
                                             DebugState.GetDispatchesAvg(), ns);
                    std::cerr.flush();
                },
                [](int count) {
                    // Same capture as the devtools "Dump frames" menu: the next guest frames are
                    // recorded and the guest pauses. The export normally happens in the devtools
                    // layer, which only runs with its menu bar shown, so wait for it here instead
                    // and answer with the frame ids. The caller sends RESUME afterwards.
                    if (DebugState.DumpingCurrentFrame() || DebugState.ShouldPauseInSubmit()) {
                        std::cerr << ";TEST_CONTROL_ERROR busy\n";
                        std::cerr.flush();
                        return;
                    }
                    DebugState.RequestFrameDump(count);
                    std::thread([] {
                        using namespace std::chrono_literals;
                        const auto deadline = std::chrono::steady_clock::now() + 60s;
                        while (std::chrono::steady_clock::now() < deadline) {
                            const auto frames = DebugState.ExportFinishedFrameDumps();
                            if (!frames.empty()) {
                                // The guest is paused by the submit that completed the dump;
                                // that pause starts after the dump is exported. Answer only
                                // once it holds, so the caller's RESUME cannot arrive first
                                // (ResumeGuestThreads is a no-op while nothing is paused).
                                while (!DebugState.IsGuestThreadsPaused() &&
                                       std::chrono::steady_clock::now() < deadline) {
                                    std::this_thread::sleep_for(10ms);
                                }
                                std::cerr << TestControl::FormatFrameDumpDone(frames);
                                std::cerr.flush();
                                return;
                            }
                            std::this_thread::sleep_for(50ms);
                        }
                        std::cerr << TestControl::FrameDumpTimeoutLine;
                        std::cerr.flush();
                    }).detach();
                },
                [](int frames) {
                    const u32 id = VideoCore::RequestScreenshotBurst(static_cast<u32>(frames));
                    std::cerr << TestControl::FormatBurstReply(id, frames);
                    std::cerr.flush();
                });
            if (status == ParseStatus::Disabled) {
                std::cerr << ";TEST_CONTROL_DISABLED\n";
            } else if (status == ParseStatus::Invalid) {
                std::cerr << ";TEST_CONTROL_ERROR invalid\n";
            }
            std::cerr.flush();
        } else if (cmd == "RUN") {
            run_semaphore.release();
        } else if (cmd == "START") {
            start_semaphore.release();
        } else if (cmd == "PATCH_MEMORY") {
            const MemoryPatcher::patchInfo entry = {
                .gameSerial = "*",
                .modNameStr = next_str(),
                .offsetStr = next_str(),
                .valueStr = next_str(),
                .targetStr = next_str(),
                .sizeStr = next_str(),
                .isOffset = next_u64() != 0,
                .littleEndian = next_u64() != 0,
                .patchMask = static_cast<MemoryPatcher::PatchMask>(next_u64()),
                .maskOffset = static_cast<int>(next_u64()),
            };
            MemoryPatcher::AddPatchToQueue(entry);
        } else if (cmd == "PAUSE") {
            DebugState.PauseGuestThreads();
        } else if (cmd == "RESUME") {
            DebugState.ResumeGuestThreads();
        } else if (cmd == "STOP") {
            SDL_Event event;
            SDL_memset(&event, 0, sizeof(event));
            event.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&event);
        } else if (cmd == "TOGGLE_FULLSCREEN") {
            SDL_Event event;
            SDL_memset(&event, 0, sizeof(event));
            event.type = SDL_EVENT_TOGGLE_FULLSCREEN;
            SDL_PushEvent(&event);
        } else if (cmd == "ADJUST_VOLUME") {
            int value = static_cast<int>(next_u64());
            bool is_game_specific = next_u64() != 0;
            EmulatorSettings.SetVolumeSlider(value, is_game_specific);
            Libraries::AudioOut::AdjustVol();
        } else if (cmd == "SET_FSR") {
            bool use_fsr = next_u64() != 0;
            if (presenter) {
                presenter->GetFsrSettingsRef().enable = use_fsr;
            }
        } else if (cmd == "SET_RCAS") {
            bool use_rcas = next_u64() != 0;
            if (presenter) {
                presenter->GetFsrSettingsRef().use_rcas = use_rcas;
            }
        } else if (cmd == "SET_RCAS_ATTENUATION") {
            int value = static_cast<int>(next_u64());
            if (presenter) {
                presenter->GetFsrSettingsRef().rcas_attenuation =
                    static_cast<float>(value / 1000.0f);
            }
        } else if (cmd == "USB_LOAD_FIGURE") {
            const auto ref = Libraries::Usbd::usb_backend->GetImplRef();
            if (ref) {
                std::string file_name = next_str();
                const u8 pad = next_u64();
                const u8 slot = next_u64();
                ref->LoadFigure(file_name, pad, slot);
            }
        } else if (cmd == "USB_REMOVE_FIGURE") {
            const auto ref = Libraries::Usbd::usb_backend->GetImplRef();
            if (ref) {
                const u8 pad = next_u64();
                const u8 slot = next_u64();
                bool full_remove = next_u64() != 0;
                ref->RemoveFigure(pad, slot, full_remove);
            }
        } else if (cmd == "USB_MOVE_FIGURE") {
            const auto ref = Libraries::Usbd::usb_backend->GetImplRef();
            if (ref) {
                const u8 new_pad = next_u64();
                const u8 new_index = next_u64();
                const u8 old_pad = next_u64();
                const u8 old_index = next_u64();
                ref->MoveFigure(new_pad, new_index, old_pad, old_index);
            }
        } else if (cmd == "USB_TEMP_REMOVE_FIGURE") {
            const auto ref = Libraries::Usbd::usb_backend->GetImplRef();
            if (ref) {
                const u8 index = next_u64();
                ref->TempRemoveFigure(index);
            }
        } else if (cmd == "USB_CANCEL_REMOVE_FIGURE") {
            const auto ref = Libraries::Usbd::usb_backend->GetImplRef();
            if (ref) {
                const u8 index = next_u64();
                ref->CancelRemoveFigure(index);
            }
        } else if (cmd == "RELOAD_INPUTS") {
            std::string config = next_str();
            Input::ParseInputConfig(config);
        } else {
            std::cerr << ";UNKNOWN CMD: " << cmd << std::endl;
        }
    }
}
