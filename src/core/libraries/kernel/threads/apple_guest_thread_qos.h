// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef __APPLE__

#include <cstdlib>
#include <string_view>

#include <pthread.h>
#include <pthread/qos.h>

namespace Libraries::Kernel {

enum class GuestThreadQosMode {
    Disabled,
    Inspect,
    UserInitiated,
};

constexpr GuestThreadQosMode ParseGuestThreadQosMode(std::string_view value) {
    if (value == "inspect") {
        return GuestThreadQosMode::Inspect;
    }
    if (value == "user-initiated") {
        return GuestThreadQosMode::UserInitiated;
    }
    return GuestThreadQosMode::Disabled;
}

inline GuestThreadQosMode GetGuestThreadQosMode() {
    static const GuestThreadQosMode mode = [] {
        const char* value = std::getenv("SHADPS4_GUEST_THREAD_QOS");
        return ParseGuestThreadQosMode(value ? std::string_view{value} : std::string_view{});
    }();
    return mode;
}

struct GuestThreadQosResult {
    qos_class_t before_class{QOS_CLASS_UNSPECIFIED};
    qos_class_t after_class{QOS_CLASS_UNSPECIFIED};
    int before_relative_priority{};
    int after_relative_priority{};
    int before_error{};
    int after_error{};
    int set_error{};
    bool set_attempted{};
};

using GuestThreadQosGetter = int (*)(pthread_t, qos_class_t*, int*);
using GuestThreadQosSetter = int (*)(qos_class_t, int);

inline GuestThreadQosResult ApplyGuestThreadQos(
    GuestThreadQosMode mode, pthread_t thread = pthread_self(),
    GuestThreadQosGetter getter = pthread_get_qos_class_np,
    GuestThreadQosSetter setter = pthread_set_qos_class_self_np) {
    GuestThreadQosResult result{};
    if (mode == GuestThreadQosMode::Disabled) {
        return result;
    }

    result.before_error = getter(thread, &result.before_class, &result.before_relative_priority);
    if (mode == GuestThreadQosMode::UserInitiated) {
        result.set_attempted = true;
        result.set_error = setter(QOS_CLASS_USER_INITIATED, 0);
    }
    result.after_error = getter(thread, &result.after_class, &result.after_relative_priority);
    return result;
}

constexpr std::string_view GuestThreadQosClassName(qos_class_t qos_class) {
    switch (qos_class) {
    case QOS_CLASS_BACKGROUND:
        return "background";
    case QOS_CLASS_UTILITY:
        return "utility";
    case QOS_CLASS_DEFAULT:
        return "default";
    case QOS_CLASS_USER_INITIATED:
        return "user-initiated";
    case QOS_CLASS_USER_INTERACTIVE:
        return "user-interactive";
    default:
        return "unspecified/unknown";
    }
}

} // namespace Libraries::Kernel

#endif // __APPLE__
