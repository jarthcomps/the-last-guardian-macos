// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <cerrno>
#include <thread>

#include "core/libraries/kernel/threads/apple_guest_thread_qos.h"

namespace {

std::atomic<int> getter_calls{};
std::atomic<int> setter_calls{};

int FailingGetter(pthread_t, qos_class_t*, int*) {
    ++getter_calls;
    return EINVAL;
}

int FailingSetter(qos_class_t, int) {
    ++setter_calls;
    return EPERM;
}

int UnexpectedSetter(qos_class_t, int) {
    ++setter_calls;
    return EINVAL;
}

bool TestModeParsing() {
    using Libraries::Kernel::GuestThreadQosMode;
    using Libraries::Kernel::ParseGuestThreadQosMode;
    return ParseGuestThreadQosMode("") == GuestThreadQosMode::Disabled &&
           ParseGuestThreadQosMode("inspect") == GuestThreadQosMode::Inspect &&
           ParseGuestThreadQosMode("user-initiated") == GuestThreadQosMode::UserInitiated &&
           ParseGuestThreadQosMode("Inspect") == GuestThreadQosMode::Disabled &&
           ParseGuestThreadQosMode("true") == GuestThreadQosMode::Disabled;
}

bool TestDisabledDoesNothing() {
    using namespace Libraries::Kernel;
    getter_calls = 0;
    setter_calls = 0;
    const auto result = ApplyGuestThreadQos(GuestThreadQosMode::Disabled, pthread_self(),
                                            FailingGetter, FailingSetter);
    return getter_calls == 0 && setter_calls == 0 && !result.set_attempted &&
           result.before_error == 0 && result.after_error == 0 && result.set_error == 0;
}

bool TestInspectLeavesNativeThreadUnchanged() {
    using namespace Libraries::Kernel;
    bool passed = true;
    std::thread worker{[&] {
        qos_class_t expected{};
        int expected_relative_priority{};
        const auto expected_error = pthread_get_qos_class_np(
            pthread_self(), &expected, &expected_relative_priority);
        setter_calls = 0;
        const auto result = ApplyGuestThreadQos(GuestThreadQosMode::Inspect, pthread_self(),
                                                pthread_get_qos_class_np, UnexpectedSetter);
        qos_class_t actual{};
        int actual_relative_priority{};
        const auto actual_error = pthread_get_qos_class_np(
            pthread_self(), &actual, &actual_relative_priority);
        passed = expected_error == 0 && actual_error == 0 &&
                 result.before_error == 0 && result.after_error == 0 &&
                 result.before_class == expected && result.after_class == expected &&
                 result.before_relative_priority == expected_relative_priority &&
                 result.after_relative_priority == expected_relative_priority &&
                 actual == expected && actual_relative_priority == expected_relative_priority &&
                 !result.set_attempted && setter_calls == 0;
    }};
    worker.join();
    return passed;
}

bool TestUserInitiatedOnNativeThread() {
    using namespace Libraries::Kernel;
    bool passed = true;
    std::thread worker{[&] {
        const auto result = ApplyGuestThreadQos(GuestThreadQosMode::UserInitiated);
        qos_class_t actual{};
        int relative_priority{};
        const auto error = pthread_get_qos_class_np(pthread_self(), &actual, &relative_priority);
        passed = result.before_error == 0 && result.set_attempted && result.set_error == 0 &&
                 result.after_error == 0 && result.after_class == QOS_CLASS_USER_INITIATED &&
                 result.after_relative_priority == 0 && error == 0 &&
                 actual == QOS_CLASS_USER_INITIATED && relative_priority == 0;
    }};
    worker.join();
    return passed;
}

bool TestSystemCallErrorsAreReported() {
    using namespace Libraries::Kernel;
    getter_calls = 0;
    setter_calls = 0;
    const auto result = ApplyGuestThreadQos(GuestThreadQosMode::UserInitiated, pthread_self(),
                                            FailingGetter, FailingSetter);
    return result.before_error == EINVAL && result.set_attempted && result.set_error == EPERM &&
           result.after_error == EINVAL && getter_calls == 2 && setter_calls == 1;
}

} // namespace

int main() {
    if (!TestModeParsing()) return 1;
    if (!TestDisabledDoesNothing()) return 2;
    if (!TestInspectLeavesNativeThreadUnchanged()) return 3;
    if (!TestUserInitiatedOnNativeThread()) return 4;
    if (!TestSystemCallErrorsAreReported()) return 5;
    return 0;
}
