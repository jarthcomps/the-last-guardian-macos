// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <csignal>
#include <iostream>
#include <string>
#include <vector>

#include "core/lazy_fault_context.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct RawFaultContext {
    uintptr_t rip;
    uintptr_t rax;
    uintptr_t rdi;
    uintptr_t fault_address;
    int signal_code;
};

struct GuestFaultContext {
    uintptr_t rip;
    uintptr_t rax;
    uintptr_t rdi;
    uintptr_t fault_address;
    int signal_code;
};

void TestDefaultOrderAndFallbackSnapshot() {
    RawFaultContext raw{0x1234, 0x55aa, 0xbbcc, 0x9876, SEGV_ACCERR};
    GuestFaultContext guest{};
    std::vector<std::string> calls;
    const bool handled = Core::DispatchMemoryFault(
        false,
        [&] {
            calls.emplace_back("dispatch");
            return false;
        },
        [&] {
            calls.emplace_back("marshal");
            guest = {raw.rip, raw.rax, raw.rdi, raw.fault_address, raw.signal_code};
        },
        [&] {
            calls.emplace_back("fallback");
            return guest.rip == raw.rip && guest.rax == raw.rax && guest.rdi == raw.rdi &&
                   guest.fault_address == raw.fault_address &&
                   guest.signal_code == raw.signal_code;
        });
    Check(handled, "default rejected fault reaches fallback with the captured context");
    Check(calls == std::vector<std::string>{"marshal", "dispatch", "fallback"},
          "default path preserves marshal-before-dispatch ordering");
}

void TestLazyConsumedFaultSkipsMarshalling() {
    int dispatches = 0;
    int marshals = 0;
    int fallbacks = 0;
    const bool handled = Core::DispatchMemoryFault(
        true,
        [&] {
            ++dispatches;
            return true;
        },
        [&] { ++marshals; },
        [&] {
            ++fallbacks;
            return false;
        });
    Check(handled, "lazy handled fault reports success");
    Check(dispatches == 1, "lazy handled fault dispatches exactly once");
    Check(marshals == 0, "lazy handled fault skips context marshalling");
    Check(fallbacks == 0, "lazy handled fault skips guest fallback");
}

void TestLazyRejectedFaultFallsThroughWithSameSnapshot() {
    RawFaultContext raw{0xabc0, 0x1111, 0x2222, 0x3333, BUS_ADRERR};
    GuestFaultContext guest{};
    int dispatches = 0;
    int fallbacks = 0;
    const bool handled = Core::DispatchMemoryFault(
        true,
        [&] {
            ++dispatches;
            return false;
        },
        [&] { guest = {raw.rip, raw.rax, raw.rdi, raw.fault_address, raw.signal_code}; },
        [&] {
            ++fallbacks;
            return guest.rip == raw.rip && guest.rax == raw.rax && guest.rdi == raw.rdi &&
                   guest.fault_address == raw.fault_address &&
                   guest.signal_code == raw.signal_code;
        });
    Check(handled, "rejected lazy fault reaches guest fallback");
    Check(dispatches == 1, "rejected lazy fault is not dispatched twice");
    Check(fallbacks == 1, "rejected lazy fault enters fallback once");
}

void TestOnlySegvAndBusUseLazyOrder() {
    constexpr int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGUSR1};
    for (int signal : signals) {
        const bool expected = signal == SIGSEGV || signal == SIGBUS;
        Check(Core::ShouldDispatchFaultBeforeContext(signal, true, SIGSEGV, SIGBUS) == expected,
              "opt-in lazy order is limited to SIGSEGV and SIGBUS");
        Check(!Core::ShouldDispatchFaultBeforeContext(signal, false, SIGSEGV, SIGBUS),
              "disabled mode preserves eager context for every signal");
    }
}

void TestOptInParserIsExact() {
    Check(Core::LazyFaultContextRequested("1"), "exact opt-in value enables lazy context");
    Check(!Core::LazyFaultContextRequested(""), "empty value leaves lazy context disabled");
    Check(!Core::LazyFaultContextRequested("0"), "zero leaves lazy context disabled");
    Check(!Core::LazyFaultContextRequested("true"), "unrecognized value leaves it disabled");
}

} // namespace

int main() {
    TestDefaultOrderAndFallbackSnapshot();
    TestLazyConsumedFaultSkipsMarshalling();
    TestLazyRejectedFaultFallsThroughWithSameSnapshot();
    TestOnlySegvAndBusUseLazyOrder();
    TestOptInParserIsExact();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "PASS: lazy fault context dispatch ordering\n";
    return 0;
}
