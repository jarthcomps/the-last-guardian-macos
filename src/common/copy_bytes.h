// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstring>

#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#endif

#if defined(__APPLE__) && defined(__x86_64__)
#include <sys/sysctl.h>
#endif

namespace Common {

/// Copies size bytes from src to dst with a plain SSE2 loop: unaligned 16-byte loads and stores,
/// 64 bytes per step. The ranges must not overlap, as with memcpy.
///
/// Ranges of 16 bytes or more end with one 16-byte move that ends at the end of the range. It
/// rewrites up to 15 bytes that the loop already copied, with the same values from the source,
/// so no byte loop is needed. Ranges under 16 bytes use memcpy.
inline void CopyBytesSse2(void* dst, const void* src, std::size_t size) noexcept {
#if defined(__SSE2__) || defined(_M_X64)
    if (size < 16) {
        std::memcpy(dst, src, size);
        return;
    }
    auto* d = static_cast<unsigned char*>(dst);
    const auto* s = static_cast<const unsigned char*>(src);
    unsigned char* const dst_end = d + size;
    const unsigned char* const src_end = s + size;
    while (size >= 64) {
        const __m128i v0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
        const __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 16));
        const __m128i v2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 32));
        const __m128i v3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 48));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d), v0);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 16), v1);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 32), v2);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 48), v3);
        s += 64;
        d += 64;
        size -= 64;
    }
    while (size >= 16) {
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d),
                         _mm_loadu_si128(reinterpret_cast<const __m128i*>(s)));
        s += 16;
        d += 16;
        size -= 16;
    }
    if (size != 0) {
        // 1 to 15 bytes left: the last 16 bytes of the range. The total is at least 16 bytes, so
        // this stays inside both ranges.
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst_end - 16),
                         _mm_loadu_si128(reinterpret_cast<const __m128i*>(src_end - 16)));
    }
#else
    std::memcpy(dst, src, size);
#endif
}

/// True when this process runs under Rosetta (an x86-64 build on Apple silicon). Under Rosetta the
/// libc memcpy measured slower than CopyBytesSse2 (see CPU-NOTES.md). Decided once per process.
inline bool CopyBytesPreferSse2() noexcept {
    static const bool prefer = [] {
#if defined(__APPLE__) && defined(__x86_64__)
        int translated = 0;
        std::size_t len = sizeof(translated);
        if (sysctlbyname("sysctl.proc_translated", &translated, &len, nullptr, 0) == 0) {
            return translated == 1;
        }
#endif
        return false;
    }();
    return prefer;
}

/// Copies size bytes like memcpy (non-overlapping ranges). Uses CopyBytesSse2 under Rosetta and
/// memcpy everywhere else.
inline void CopyBytes(void* dst, const void* src, std::size_t size) noexcept {
    if (CopyBytesPreferSse2()) {
        CopyBytesSse2(dst, src, size);
    } else {
        std::memcpy(dst, src, size);
    }
}

} // namespace Common
