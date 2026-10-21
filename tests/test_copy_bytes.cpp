// Tests for common/copy_bytes.h: CopyBytesSse2 and CopyBytes must copy exactly size bytes, for
// every size from 0 to 600, larger sizes around the block boundaries, and every pair of
// source and destination offsets. Bytes outside the destination range must not change, and the
// source must not change. Build with -fsanitize=address as well: exact-size heap buffers make
// any read past the source or write past the destination an error.
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

#include "common/copy_bytes.h"

namespace {

using Copy = void (*)(void*, const void*, std::size_t) noexcept;

void CopyWithSse2(void* dst, const void* src, std::size_t size) noexcept {
    Common::CopyBytesSse2(dst, src, size);
}

void CopyWithDispatch(void* dst, const void* src, std::size_t size) noexcept {
    Common::CopyBytes(dst, src, size);
}

// Copies n bytes from a source at offset soff of a buffer to a destination at offset doff of a
// buffer with guard bytes on both sides, and checks the result.
void CheckCopy(Copy copy, std::mt19937& rng, std::size_t n, std::size_t soff, std::size_t doff) {
    constexpr std::size_t Guard = 64;
    std::vector<std::uint8_t> src_buf(soff + n + Guard);
    for (auto& b : src_buf) {
        b = static_cast<std::uint8_t>(rng());
    }
    const std::vector<std::uint8_t> src_before = src_buf;

    std::vector<std::uint8_t> dst_buf(doff + n + 2 * Guard, 0xAB);
    const std::vector<std::uint8_t> dst_before = dst_buf;

    copy(dst_buf.data() + Guard + doff, src_buf.data() + soff, n);

    assert(std::memcmp(dst_buf.data() + Guard + doff, src_buf.data() + soff, n) == 0);
    assert(std::memcmp(dst_buf.data(), dst_before.data(), Guard + doff) == 0);
    assert(std::memcmp(dst_buf.data() + Guard + doff + n, dst_before.data() + Guard + doff + n,
                       dst_buf.size() - (Guard + doff + n)) == 0);
    assert(src_buf == src_before);
}

// Exact-size heap buffers, so AddressSanitizer reports any access beyond the copied range.
void CheckExactCopy(Copy copy, std::mt19937& rng, std::size_t n) {
    auto src = std::make_unique<std::uint8_t[]>(n == 0 ? 1 : n);
    auto dst = std::make_unique<std::uint8_t[]>(n == 0 ? 1 : n);
    std::vector<std::uint8_t> expected(n);
    for (std::size_t i = 0; i < n; i++) {
        src[i] = static_cast<std::uint8_t>(rng());
        expected[i] = src[i];
    }
    copy(dst.get(), src.get(), n);
    assert(n == 0 || std::memcmp(dst.get(), expected.data(), n) == 0);
}

} // namespace

int main() {
    std::mt19937 rng{20261020};
    const Copy copies[] = {CopyWithSse2, CopyWithDispatch};

    for (const Copy copy : copies) {
        // Every size from 0 to 600 at aligned and misaligned offsets.
        for (std::size_t n = 0; n <= 600; n++) {
            for (std::size_t soff = 0; soff < 3; soff++) {
                for (std::size_t doff = 0; doff < 3; doff++) {
                    CheckCopy(copy, rng, n, soff, doff);
                }
            }
            CheckExactCopy(copy, rng, n);
        }

        // Block boundaries and larger sizes, with odd offsets.
        const std::size_t big[] = {1000, 1023, 1024, 1025, 4095, 4096, 4097, 16383, 16384,
                                   16385, 65536, 65537, 65551};
        for (const std::size_t n : big) {
            CheckCopy(copy, rng, n, 0, 0);
            CheckCopy(copy, rng, n, 5, 11);
            CheckCopy(copy, rng, n, 15, 1);
            CheckExactCopy(copy, rng, n);
        }

        // Random sizes and offsets.
        for (int it = 0; it < 20000; it++) {
            const std::size_t n = rng() % 5000;
            CheckCopy(copy, rng, n, rng() % 64, rng() % 64);
        }
    }

    // The dispatch decision is stable for the process.
    assert(Common::CopyBytesPreferSse2() == Common::CopyBytesPreferSse2());
    return 0;
}
