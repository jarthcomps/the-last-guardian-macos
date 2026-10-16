// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <pthread.h>
#include <pthread/qos.h>
#include <string>
#include "common/thread.h"

namespace Common {

constexpr qos_class_t AppleThreadQos(ThreadPriority priority) {
    switch (priority) {
    case ThreadPriority::Low:
        return QOS_CLASS_UTILITY;
    case ThreadPriority::High:
        return QOS_CLASS_USER_INITIATED;
    case ThreadPriority::VeryHigh:
    case ThreadPriority::Critical:
        return QOS_CLASS_USER_INTERACTIVE;
    case ThreadPriority::Normal:
    default:
        return QOS_CLASS_DEFAULT;
    }
}

inline int SetAppleThreadPriority(ThreadPriority priority) {
    return pthread_set_qos_class_self_np(AppleThreadQos(priority), 0);
}

} // namespace Common
