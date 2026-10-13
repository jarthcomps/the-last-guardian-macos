#include <array>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include "core/rosetta_avx_store.h"

static void* watched;
static size_t page_size;
static volatile sig_atomic_t faults;

static void on_fault(int, siginfo_t*, void*) {
    faults = faults + 1;
    mprotect(watched, page_size * 2, PROT_READ | PROT_WRITE);
    asm volatile("vzeroupper" : : : "ymm0", "ymm1", "ymm2", "ymm3", "ymm4", "ymm5", "ymm6", "ymm7", "ymm8", "ymm9", "ymm10", "ymm11", "ymm12", "ymm13", "ymm14", "ymm15");
}

struct State {
    std::array<unsigned char, 512> vectors;
    uint64_t flags;
    uint64_t r11;
    std::array<uint64_t, 16> red_zone;
};

int main() {
    using namespace Xbyak::util;
    page_size = static_cast<size_t>(getpagesize());
    watched = mmap(nullptr, page_size * 2, PROT_READ | PROT_WRITE,
                   MAP_ANON | MAP_PRIVATE, -1, 0);
    if (watched == MAP_FAILED) return 2;
    struct sigaction handler{};
    handler.sa_sigaction = on_fault;
    handler.sa_flags = SA_SIGINFO;
    sigemptyset(&handler.sa_mask);
    sigaction(SIGSEGV, &handler, nullptr);
    sigaction(SIGBUS, &handler, nullptr);
    alignas(32) std::array<unsigned char, 512> expected;
    for (size_t i = 0; i < expected.size(); ++i) expected[i] = i * 29 + i / 32;
    for (int source = 0; source < 16; ++source) {
        Xbyak::CodeGenerator code;
        for (int i = 0; i < 16; ++i) code.vmovups(Xbyak::Ymm(i), ptr[rsi + i * 32]);
        code.mov(r11, 0x13579abcdef24680ULL);
        for (int i = 1; i <= 16; ++i) code.mov(ptr[rsp - i * 8], r11);
        code.xor_(r8, r8);
        code.stc();
        Core::EmitRosettaAvxStore(code, source, [](auto& c, auto address, int) {
            c.lea(address, ptr[rdi]);
        });
        for (int i = 1; i <= 16; ++i) {
            code.mov(r8, ptr[rsp - i * 8]);
            code.mov(ptr[rdx + 528 + (i - 1) * 8], r8);
        }
        code.pushfq();
        code.pop(r8);
        code.mov(ptr[rdx + 512], r8);
        code.mov(ptr[rdx + 520], r11);
        for (int i = 0; i < 16; ++i) code.vmovups(ptr[rdx + i * 32], Xbyak::Ymm(i));
        code.vzeroupper();
        code.ret();
        code.ready();
        auto run = code.getCode<void (*)(void*, const void*, State*)>();
        for (size_t offset : {size_t(17), page_size - 16, page_size - 15, page_size - 1}) {
            std::memset(watched, 0, page_size * 2);
            if (offset == 17) mprotect(watched, page_size * 2, PROT_READ);
            else mprotect(static_cast<unsigned char*>(watched) + page_size, page_size, PROT_NONE);
            State observed{};
            auto* destination = static_cast<unsigned char*>(watched) + offset;
            run(destination, expected.data(), &observed);
            if (std::memcmp(destination, expected.data() + source * 32, 32) != 0 ||
                observed.vectors != expected || (observed.flags & 0x8c5) != 0x45 ||
                observed.r11 != 0x13579abcdef24680ULL) {
                std::printf("State mismatch: YMM%d, offset %zu\n", source, offset);
                return 3;
            }
            for (auto value : observed.red_zone) if (value != 0x13579abcdef24680ULL) return 4;
        }
    }
    std::printf("PASS: all 16 YMM registers, flags, R11 and red zone preserved across %d faults\n",
                static_cast<int>(faults));
    munmap(watched, page_size * 2);
    return faults == 64 ? 0 : 5;
}
