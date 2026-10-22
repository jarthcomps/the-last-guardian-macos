// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <charconv>
#include <string>
#include <string_view>
#include <vector>
#include <system_error>
#include <utility>

namespace IPCTestControl {

enum class Button {
    Cross,
    Circle,
    Square,
    Triangle,
    Options,
    Up,
    Down,
    Left,
    Right,
    L1,
    R1,
    L2,
    R2,
    L3,
    R3,
    TouchPad,
    /// Not a button: pressed connects the slot's controller, released disconnects it.
    Connect,
};

enum class Axis { LeftX, LeftY, RightX, RightY };

struct Action {
    enum class Kind { Button, Axis, Screenshot, Metrics, FrameDump, ScreenshotBurst } kind;
    int slot{};
    Button button{};
    Axis axis{};
    int value{};
};

enum class ParseStatus { NotTestCommand, Disabled, Invalid, Parsed };

struct ParseResult {
    ParseStatus status{ParseStatus::NotTestCommand};
    Action action{};
};

inline bool Enabled(bool ipc_enabled, std::string_view environment_value) {
    return ipc_enabled && environment_value == "1";
}

/// Reply once the frame dumps requested with TEST_FRAME_DUMP are written:
/// ";TEST_FRAME_DUMP_DONE <count> <frame id>..." (files log/frame_dump_<id>.csv).
inline std::string FormatFrameDumpDone(const std::vector<unsigned>& frame_ids) {
    std::string line = ";TEST_FRAME_DUMP_DONE " + std::to_string(frame_ids.size());
    for (const unsigned id : frame_ids) {
        line += ' ';
        line += std::to_string(id);
    }
    line += '\n';
    return line;
}

/// Reply to TEST_SCREENSHOT_BURST: ";TEST_SCREENSHOT_BURST <id> <frames>" (id 0 = busy).
inline std::string FormatBurstReply(unsigned burst_id, int frames) {
    return ";TEST_SCREENSHOT_BURST " + std::to_string(burst_id) + " " + std::to_string(frames) +
           "\n";
}

/// Reply when the requested dump did not finish within the wait limit.
inline constexpr std::string_view FrameDumpTimeoutLine = ";TEST_FRAME_DUMP_TIMEOUT\n";

namespace Detail {

inline bool ParseInteger(std::string_view text, int& value) {
    if (text.empty()) {
        return false;
    }
    const char* first = text.data();
    const char* last = first + text.size();
    const auto result = std::from_chars(first, last, value, 10);
    return result.ec == std::errc{} && result.ptr == last;
}

inline bool TakeToken(std::string_view& input, std::string_view& token) {
    if (input.empty() || input.front() == ' ') {
        return false;
    }
    const size_t end = input.find(' ');
    token = input.substr(0, end);
    if (end == std::string_view::npos) {
        input = {};
    } else {
        input.remove_prefix(end + 1);
    }
    return true;
}

inline bool ParseButton(std::string_view text, Button& button) {
    constexpr std::pair<std::string_view, Button> names[] = {
        {"cross", Button::Cross},       {"circle", Button::Circle},
        {"square", Button::Square},     {"triangle", Button::Triangle},
        {"options", Button::Options},   {"up", Button::Up},
        {"down", Button::Down},         {"left", Button::Left},
        {"right", Button::Right},       {"l1", Button::L1},
        {"r1", Button::R1},             {"l2", Button::L2},
        {"r2", Button::R2},             {"l3", Button::L3},
        {"r3", Button::R3},             {"touchpad", Button::TouchPad},
        {"connect", Button::Connect},
    };
    for (const auto& [name, value] : names) {
        if (text == name) {
            button = value;
            return true;
        }
    }
    return false;
}

inline bool ParseAxis(std::string_view text, Axis& axis) {
    constexpr std::pair<std::string_view, Axis> names[] = {
        {"left_x", Axis::LeftX}, {"left_y", Axis::LeftY},
        {"right_x", Axis::RightX}, {"right_y", Axis::RightY},
    };
    for (const auto& [name, value] : names) {
        if (text == name) {
            axis = value;
            return true;
        }
    }
    return false;
}

} // namespace Detail

/// Parse one complete IPC line. Test commands use a strict single-line grammar and decimal fields.
inline ParseResult Parse(std::string_view line, bool enabled) {
    if (!line.starts_with("TEST_")) {
        return {};
    }
    if (!enabled) {
        return {.status = ParseStatus::Disabled};
    }
    if (line.back() == ' ') {
        return {.status = ParseStatus::Invalid};
    }

    std::string_view rest = line;
    std::string_view command;
    if (!Detail::TakeToken(rest, command)) {
        return {.status = ParseStatus::Invalid};
    }
    if (command == "TEST_METRICS") {
        return rest.empty() ? ParseResult{.status = ParseStatus::Parsed,
                                          .action = {.kind = Action::Kind::Metrics}}
                            : ParseResult{.status = ParseStatus::Invalid};
    }
    if (command == "TEST_SCREENSHOT") {
        return rest.empty() ? ParseResult{.status = ParseStatus::Parsed,
                                          .action = {.kind = Action::Kind::Screenshot}}
                            : ParseResult{.status = ParseStatus::Invalid};
    }
    if (command == "TEST_SCREENSHOT_BURST") {
        // Frame count, 1 to 600: consecutive presented frames saved as raw RGBA.
        std::string_view count_text;
        int count = -1;
        if (!Detail::TakeToken(rest, count_text) || !rest.empty() ||
            !Detail::ParseInteger(count_text, count) || count < 1 || count > 600) {
            return {.status = ParseStatus::Invalid};
        }
        return {.status = ParseStatus::Parsed,
                .action = {.kind = Action::Kind::ScreenshotBurst, .value = count}};
    }
    if (command == "TEST_FRAME_DUMP") {
        // Optional frame count, 1 to 5 like the devtools "Dump frames" slider. The dump pauses
        // the guest after the captured frames; the caller resumes it with RESUME.
        int count = 1;
        if (!rest.empty()) {
            std::string_view count_text;
            if (!Detail::TakeToken(rest, count_text) || !rest.empty() ||
                !Detail::ParseInteger(count_text, count) || count < 1 || count > 5) {
                return {.status = ParseStatus::Invalid};
            }
        }
        return {.status = ParseStatus::Parsed,
                .action = {.kind = Action::Kind::FrameDump, .value = count}};
    }

    std::string_view slot_text;
    int slot = -1;
    if (!Detail::TakeToken(rest, slot_text) || !Detail::ParseInteger(slot_text, slot) || slot < 0 ||
        slot > 3) {
        return {.status = ParseStatus::Invalid};
    }

    if (command == "TEST_PAD_BUTTON") {
        std::string_view name;
        std::string_view pressed_text;
        int pressed = -1;
        Button button{};
        if (!Detail::TakeToken(rest, name) || !Detail::TakeToken(rest, pressed_text) ||
            !rest.empty() || !Detail::ParseButton(name, button) ||
            !Detail::ParseInteger(pressed_text, pressed) || (pressed != 0 && pressed != 1)) {
            return {.status = ParseStatus::Invalid};
        }
        return {.status = ParseStatus::Parsed,
                .action = {.kind = Action::Kind::Button,
                           .slot = slot,
                           .button = button,
                           .value = pressed}};
    }

    if (command == "TEST_PAD_AXIS") {
        std::string_view name;
        std::string_view value_text;
        int value = -1;
        Axis axis{};
        if (!Detail::TakeToken(rest, name) || !Detail::TakeToken(rest, value_text) ||
            !rest.empty() || !Detail::ParseAxis(name, axis) ||
            !Detail::ParseInteger(value_text, value) || value < 0 || value > 255) {
            return {.status = ParseStatus::Invalid};
        }
        return {.status = ParseStatus::Parsed,
                .action = {.kind = Action::Kind::Axis, .slot = slot, .axis = axis, .value = value}};
    }
    return {.status = ParseStatus::Invalid};
}

template <typename ButtonCallback, typename AxisCallback, typename ScreenshotCallback,
          typename MetricsCallback, typename FrameDumpCallback, typename BurstCallback>
ParseStatus Dispatch(std::string_view line, bool enabled, ButtonCallback&& button_callback,
                     AxisCallback&& axis_callback, ScreenshotCallback&& screenshot_callback,
                     MetricsCallback&& metrics_callback, FrameDumpCallback&& frame_dump_callback,
                     BurstCallback&& burst_callback) {
    const ParseResult result = Parse(line, enabled);
    if (result.status != ParseStatus::Parsed) {
        return result.status;
    }
    switch (result.action.kind) {
    case Action::Kind::Button:
        button_callback(result.action.slot, result.action.button, result.action.value != 0);
        break;
    case Action::Kind::Axis:
        axis_callback(result.action.slot, result.action.axis, result.action.value);
        break;
    case Action::Kind::Screenshot:
        screenshot_callback();
        break;
    case Action::Kind::Metrics:
        metrics_callback();
        break;
    case Action::Kind::FrameDump:
        frame_dump_callback(result.action.value);
        break;
    case Action::Kind::ScreenshotBurst:
        burst_callback(result.action.value);
        break;
    }
    return ParseStatus::Parsed;
}

} // namespace IPCTestControl
