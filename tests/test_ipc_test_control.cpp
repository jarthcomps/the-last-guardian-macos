// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cassert>
#include <string_view>
#include <tuple>
#include <vector>

class IPC;
#include "core/ipc/test_control.h"

using IPCTestControl::Action;
using IPCTestControl::Axis;
using IPCTestControl::Button;
using IPCTestControl::Dispatch;
using IPCTestControl::ParseStatus;

int main() {
    assert(!IPCTestControl::Enabled(false, "1"));
    assert(!IPCTestControl::Enabled(true, "true"));
    assert(!IPCTestControl::Enabled(true, "01"));
    assert(IPCTestControl::Enabled(true, "1"));

    std::vector<std::tuple<int, Button, bool>> buttons;
    std::vector<std::tuple<int, Axis, int>> axes;
    int screenshot_requests = 0;
    int metric_requests = 0;
    std::vector<int> frame_dumps;
    const auto button = [&](int slot, Button value, bool pressed) {
        buttons.emplace_back(slot, value, pressed);
    };
    const auto axis = [&](int slot, Axis value, int amount) {
        axes.emplace_back(slot, value, amount);
    };
    const auto metrics = [&] { ++metric_requests; };
    const auto screenshot = [&] { ++screenshot_requests; };
    const auto frame_dump = [&](int count) { frame_dumps.push_back(count); };
    std::vector<int> bursts;
    const auto burst = [&](int count) { bursts.push_back(count); };

    assert(Dispatch("TEST_PAD_BUTTON 0 cross 1", true, button, axis, screenshot, metrics, frame_dump, burst) ==
           ParseStatus::Parsed);
    assert(buttons.size() == 1);
    assert((buttons.back() == std::tuple{0, Button::Cross, true}));
    assert(Dispatch("TEST_PAD_BUTTON 0 cross 0", true, button, axis, screenshot, metrics, frame_dump, burst) ==
           ParseStatus::Parsed);
    assert((buttons.back() == std::tuple{0, Button::Cross, false}));

    assert(Dispatch("TEST_PAD_AXIS 3 right_y 0", true, button, axis, screenshot, metrics, frame_dump, burst) ==
           ParseStatus::Parsed);
    assert((axes.back() == std::tuple{3, Axis::RightY, 0}));
    assert(Dispatch("TEST_PAD_AXIS 0 right_y 255", true, button, axis, screenshot, metrics, frame_dump, burst) ==
           ParseStatus::Parsed);
    assert((axes.back() == std::tuple{0, Axis::RightY, 255}));

    assert(Dispatch("TEST_METRICS", true, button, axis, screenshot, metrics, frame_dump, burst) ==
           ParseStatus::Parsed);
    assert(metric_requests == 1);

    assert(Dispatch("TEST_SCREENSHOT", true, button, axis, screenshot, metrics, frame_dump, burst) ==
           ParseStatus::Parsed);
    assert(screenshot_requests == 1);

    assert(Dispatch("TEST_FRAME_DUMP", true, button, axis, screenshot, metrics, frame_dump, burst) ==
           ParseStatus::Parsed);
    assert((frame_dumps == std::vector<int>{1}));
    assert(Dispatch("TEST_FRAME_DUMP 5", true, button, axis, screenshot, metrics, frame_dump, burst) ==
           ParseStatus::Parsed);
    assert((frame_dumps == std::vector<int>{1, 5}));

    const size_t buttons_before_errors = buttons.size();
    const size_t axes_before_errors = axes.size();
    const auto invalid = [&](std::string_view command) {
        assert(Dispatch(command, true, button, axis, screenshot, metrics, frame_dump, burst) ==
               ParseStatus::Invalid);
    };
    invalid("TEST_PAD_BUTTON 4 cross 1");
    invalid("TEST_PAD_BUTTON -1 cross 1");
    invalid("TEST_PAD_BUTTON 0 nope 1");
    invalid("TEST_PAD_BUTTON 0 cross 2");
    invalid("TEST_PAD_BUTTON 0 cross 1 trailing");
    invalid("TEST_PAD_BUTTON 0 cross 2147483648");
    invalid("TEST_PAD_AXIS 0 right_y -1");
    invalid("TEST_PAD_AXIS 0 right_y 256");
    invalid("TEST_PAD_AXIS 0 right_y 999999999999999999999999");
    invalid("TEST_PAD_AXIS 0 right_y 128 extra");
    invalid("TEST_PAD_AXIS 0 right_y 128 ");
    invalid("TEST_METRICS trailing");
    invalid("TEST_SCREENSHOT trailing");
    invalid("TEST_SCREENSHOT_BURST");
    invalid("TEST_SCREENSHOT_BURST 0");
    invalid("TEST_SCREENSHOT_BURST 601");
    invalid("TEST_SCREENSHOT_BURST 10 extra");
    invalid("TEST_FRAME_DUMP 0");
    invalid("TEST_FRAME_DUMP 6");
    invalid("TEST_FRAME_DUMP -1");
    invalid("TEST_FRAME_DUMP two");
    invalid("TEST_FRAME_DUMP 2 extra");
    invalid("TEST_FRAME_DUMP 2 ");
    invalid("TEST_FRAME_DUMP 99999999999999999999");
    invalid("TEST_UNKNOWN");
    assert(buttons.size() == buttons_before_errors);
    assert(axes.size() == axes_before_errors);
    assert(metric_requests == 1);
    assert(screenshot_requests == 1);
    assert((frame_dumps == std::vector<int>{1, 5}));

    assert(Dispatch("TEST_PAD_BUTTON 0 cross 1", false, button, axis, screenshot, metrics, frame_dump, burst) ==
           ParseStatus::Disabled);
    assert(Dispatch("TEST_NOT_A_COMMAND", false, button, axis, screenshot, metrics, frame_dump, burst) ==
           ParseStatus::Disabled);
    assert(Dispatch("TEST_SCREENSHOT", false, button, axis, screenshot, metrics, frame_dump, burst) ==
           ParseStatus::Disabled);
    assert(Dispatch("TEST_FRAME_DUMP 2", false, button, axis, screenshot, metrics, frame_dump, burst) ==
           ParseStatus::Disabled);
    assert((frame_dumps == std::vector<int>{1, 5}));
    assert(buttons.size() == buttons_before_errors);
    assert(axes.size() == axes_before_errors);
    assert(metric_requests == 1);
    assert(screenshot_requests == 1);
    assert(Dispatch("RUN", false, button, axis, screenshot, metrics, frame_dump, burst) ==
           ParseStatus::NotTestCommand);

    assert(IPCTestControl::FormatFrameDumpDone({}) == ";TEST_FRAME_DUMP_DONE 0\n");
    assert(IPCTestControl::FormatFrameDumpDone({4096}) == ";TEST_FRAME_DUMP_DONE 1 4096\n");
    assert(IPCTestControl::FormatFrameDumpDone({7, 8, 9}) == ";TEST_FRAME_DUMP_DONE 3 7 8 9\n");
    assert(IPCTestControl::FrameDumpTimeoutLine == ";TEST_FRAME_DUMP_TIMEOUT\n");

    assert(Dispatch("TEST_SCREENSHOT_BURST 90", true, button, axis, screenshot, metrics,
                    frame_dump, burst) == ParseStatus::Parsed);
    assert(Dispatch("TEST_SCREENSHOT_BURST 600", true, button, axis, screenshot, metrics,
                    frame_dump, burst) == ParseStatus::Parsed);
    assert((bursts == std::vector<int>{90, 600}));
    assert(Dispatch("TEST_SCREENSHOT_BURST 5", false, button, axis, screenshot, metrics,
                    frame_dump, burst) == ParseStatus::Disabled);
    assert(bursts.size() == 2);
    assert(IPCTestControl::FormatBurstReply(3, 90) == ";TEST_SCREENSHOT_BURST 3 90\n");
    assert(IPCTestControl::FormatBurstReply(0, 5) == ";TEST_SCREENSHOT_BURST 0 5\n");
}
