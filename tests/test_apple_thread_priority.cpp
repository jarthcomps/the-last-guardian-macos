// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <thread>
#include "common/apple_thread_priority.h"

int main() {
    using Common::ThreadPriority;
    const std::array cases{
        std::pair{ThreadPriority::Low, QOS_CLASS_UTILITY},
        std::pair{ThreadPriority::Normal, QOS_CLASS_DEFAULT},
        std::pair{ThreadPriority::High, QOS_CLASS_USER_INITIATED},
        std::pair{ThreadPriority::VeryHigh, QOS_CLASS_USER_INTERACTIVE},
        std::pair{ThreadPriority::Critical, QOS_CLASS_USER_INTERACTIVE},
        std::pair{static_cast<ThreadPriority>(99), QOS_CLASS_DEFAULT},
        std::pair{static_cast<ThreadPriority>(~0U), QOS_CLASS_DEFAULT},
    };
    for (const auto& [priority, expected] : cases) {
        int error = 0;
        std::thread worker{[&] {
            if (Common::SetAppleThreadPriority(priority) != 0) {
                error = 1;
                return;
            }
            qos_class_t actual{};
            int relative_priority{};
            if (pthread_get_qos_class_np(pthread_self(), &actual, &relative_priority) != 0 ||
                actual != expected || relative_priority != 0) {
                error = 2;
            }
        }};
        worker.join();
        if (error != 0) return error;
    }
    return 0;
}
