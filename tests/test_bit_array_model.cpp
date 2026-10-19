// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Randomized check of Common::BitArray against a plain bool model. Built both with and without
// AVX2 (macOS engines use the scalar path since SHADPS4_X86_MARCH=x86-64-v2).
#include <cstdio>
#include <random>
#include <utility>
#include <vector>
#include "common/bit_array.h"

constexpr size_t N = 4096;
using BA = Common::BitArray<N>;

static std::pair<size_t, size_t> model_first(const std::vector<bool>& m, size_t from) {
    size_t s = from;
    while (s < N && !m[s]) ++s;
    if (s >= N) return {N, N};
    size_t e = s;
    while (e < N && m[e]) ++e;
    return {s, e};
}

static std::pair<size_t, size_t> model_last(const std::vector<bool>& m, size_t end) {
    size_t e = end;
    while (e > 0 && !m[e - 1]) --e;
    if (e == 0) return {0, 0};
    size_t s = e;
    while (s > 0 && m[s - 1]) --s;
    return {s, e};
}

int main() {
    std::mt19937_64 rng(12345);
    for (int round = 0; round < 2000; ++round) {
        BA a;
        std::vector<bool> m(N, false);
        for (int op = 0; op < 40; ++op) {
            size_t x = rng() % (N + 1), y = rng() % (N + 1);
            if (x > y) std::swap(x, y);
            const int kind = rng() % 3;
            if (kind == 0) { a.SetRange(x, y); for (size_t i = x; i < y; ++i) m[i] = true; }
            else if (kind == 1) { a.UnsetRange(x, y); for (size_t i = x; i < y; ++i) m[i] = false; }
            else { const size_t i = rng() % N; a.Set(i); m[i] = true; }
            // queries
            size_t q = rng() % (N + 1), r = rng() % (N + 1);
            if (q > r) std::swap(q, r);
            bool any = false;
            for (size_t i = q; i < r; ++i) any |= m[i];
            if (a.AnyInRange(q, r) != any) { std::printf("AnyInRange %zu %zu\n", q, r); return 1; }
            const auto f = a.FirstRangeFrom(q);
            const auto mf = model_first(m, q);
            if (mf.first < N && f != mf) { std::printf("FirstRangeFrom %zu\n", q); return 2; }
            const auto l = a.LastRangeFrom(r);
            const auto ml = model_last(m, r);
            if (ml.second > 0 && l != ml) { std::printf("LastRangeFrom %zu\n", r); return 3; }
            BA sub(a, q, r);
            for (size_t i = 0; i < N; ++i) {
                if (sub.Get(i) != (i >= q && i < r && m[i])) { std::printf("sub %zu\n", i); return 4; }
            }
            bool none = true;
            for (bool b : m) none &= !b;
            if (a.None() != none || a.Any() == none) return 5;
        }
    }
    std::printf("bit array model: ok\n");
    return 0;
}
