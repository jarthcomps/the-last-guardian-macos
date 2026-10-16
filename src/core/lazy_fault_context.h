// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string_view>
#include <utility>

namespace Core {

// In lazy mode, handlers that reject the fault must leave the raw machine context untouched,
// because the guest snapshot is produced after dispatch. A handled fault skips that snapshot.
template <typename Dispatch, typename Marshal, typename Fallback>
bool DispatchMemoryFault(bool lazy_context, Dispatch&& dispatch, Marshal&& marshal,
                         Fallback&& fallback) {
    if (lazy_context && std::forward<Dispatch>(dispatch)()) {
        return true;
    }
    std::forward<Marshal>(marshal)();
    if (!lazy_context && std::forward<Dispatch>(dispatch)()) {
        return true;
    }
    return std::forward<Fallback>(fallback)();
}

constexpr bool IsMemoryFaultSignal(int signal, int sigsegv, int sigbus) noexcept {
    return signal == sigsegv || signal == sigbus;
}

constexpr bool LazyFaultContextRequested(std::string_view value) noexcept {
    return value == "1";
}

constexpr bool ShouldDispatchFaultBeforeContext(int signal, bool opt_in, int sigsegv,
                                               int sigbus) noexcept {
    return opt_in && IsMemoryFaultSignal(signal, sigsegv, sigbus);
}

} // namespace Core
