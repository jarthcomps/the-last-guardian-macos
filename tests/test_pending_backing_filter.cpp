// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// PendingBackingCopies::MayOverlap is the lock-free filter in front of Overlaps. Single-threaded
// checks compare both against a model; the two-thread check follows the real protocol: one thread
// adds records and queries (as the command processor does), a worker writes the bytes back and
// then removes the record (as the download callbacks do).

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <random>
#include <span>
#include <thread>
#include <vector>
#include "video_core/buffer_cache/download_overlap.h"

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); std::exit(1); \
} } while (false)

namespace {

using VideoCore::PendingBackingCopies;
using Range = PendingBackingCopies::Range;
constexpr std::uintptr_t Shift = PendingBackingCopies::GranuleShift;
constexpr std::uintptr_t GranuleBytes = std::uintptr_t{1} << Shift;
constexpr std::uintptr_t Max = ~std::uintptr_t{0};

void TestBasics() {
    PendingBackingCopies pending;
    CHECK(!pending.MayOverlap(0x1800, 0));
    CHECK(!pending.MayOverlap(0x1800, 8));
    CHECK(!pending.Overlaps(0x1800, 8));
    // A query too wide for the filter is a maybe; the exact check still decides.
    CHECK(pending.MayOverlap(0x1800, 0x10000));
    CHECK(!pending.Overlaps(0x1800, 0x10000));
    // A query that wraps past the end of the address space is a maybe.
    CHECK(pending.MayOverlap(Max - 3, 8));
    CHECK(!pending.Overlaps(Max - 3, 8));

    const std::array first{Range{0x1800, 0x1810}}; // granule 0x18
    pending.Add(1, first);
    CHECK(pending.MayOverlap(0x1800, 8));
    CHECK(pending.Overlaps(0x1800, 8));
    CHECK(pending.MayOverlap(0x17fc, 8)); // straddles granules 0x17 and 0x18
    CHECK(pending.Overlaps(0x17fc, 8));
    CHECK(pending.MayOverlap(0x1820, 8)); // same granule, other bytes: a maybe
    CHECK(!pending.Overlaps(0x1820, 8));
    CHECK(!pending.MayOverlap(0x17f8, 8)); // granule 0x17 only
    CHECK(!pending.Overlaps(0x17f8, 8));
    CHECK(!pending.MayOverlap(0x1900, 8)); // granule 0x19
    CHECK(!pending.Overlaps(0x1900, 8));

    const std::array second{Range{0x2100, 0x2500}}; // granules 0x21 to 0x24
    pending.Add(2, second);
    CHECK(pending.MayOverlap(0x2300, 4));
    CHECK(pending.Overlaps(0x2300, 4));
    CHECK(!pending.MayOverlap(0x2500, 4));
    CHECK(!pending.Overlaps(0x2500, 4));

    // Empty, zero-length and inverted ranges never overlap and leave no counts behind.
    const std::array empty{Range{0x3000, 0x3000}, Range{0x3100, 0x3080}};
    pending.Add(3, empty);
    CHECK(!pending.MayOverlap(0x3000, 0x200));
    CHECK(!pending.Overlaps(0x3000, 0x200));
    pending.Add(4, std::span<const Range>{});
    CHECK(!pending.MayOverlap(0x3000, 0x200));

    pending.Remove(999); // Never added: nothing changes.
    CHECK(pending.Overlaps(0x1800, 8));
    pending.Remove(1);
    CHECK(!pending.MayOverlap(0x1800, 8));
    CHECK(!pending.Overlaps(0x1800, 8));
    CHECK(pending.MayOverlap(0x2300, 4));
    pending.Remove(2);
    pending.Remove(3);
    pending.Remove(4);
    CHECK(!pending.MayOverlap(0x2300, 4));
    CHECK(!pending.MayOverlap(0x3000, 0x200));
}

void TestCounts() {
    // Granules FilterSlots apart share a counter. The counter must count: removing one of two
    // records on the same counter must leave the other one visible.
    constexpr std::uintptr_t Alias = std::uintptr_t{PendingBackingCopies::FilterSlots} << Shift;
    const std::uintptr_t a = 0x1800;
    const std::uintptr_t b = a + Alias;
    PendingBackingCopies pending;
    const std::array rec_a{Range{a, a + 16}};
    const std::array rec_b{Range{b, b + 16}};
    pending.Add(1, rec_a);
    pending.Add(2, rec_b);
    CHECK(pending.MayOverlap(a, 8));
    CHECK(pending.MayOverlap(b, 8));
    pending.Remove(1);
    CHECK(!pending.Overlaps(a, 8));
    CHECK(pending.MayOverlap(a, 8)); // shares a counter with record 2: a maybe
    CHECK(pending.Overlaps(b, 8));
    CHECK(pending.MayOverlap(b, 8));
    pending.Remove(2);
    CHECK(!pending.MayOverlap(a, 8));
    CHECK(!pending.MayOverlap(b, 8));

    // Two records on one granule: removing one must keep the granule counted.
    const std::array half_a{Range{a, a + 8}};
    const std::array half_b{Range{a + 8, a + 16}};
    pending.Add(3, half_a);
    pending.Add(4, half_b);
    pending.Remove(3);
    CHECK(pending.MayOverlap(a, 8));
    CHECK(!pending.Overlaps(a, 8));
    CHECK(pending.Overlaps(a + 8, 8));
    pending.Remove(4);
    CHECK(!pending.MayOverlap(a, 16));
}

void TestWide() {
    // A record covering more granules than the filter counts sets the wide counter: every query
    // is then a maybe until that record is removed.
    PendingBackingCopies pending;
    const std::array wide{Range{0x100000, 0x100000 + (std::uintptr_t{2} << 20)}};
    pending.Add(1, wide);
    CHECK(pending.MayOverlap(0x9000000, 8));
    CHECK(!pending.Overlaps(0x9000000, 8));
    CHECK(pending.Overlaps(0x100100, 8));
    CHECK(pending.MayOverlap(0x100100, 8));
    pending.Remove(1);
    CHECK(!pending.MayOverlap(0x9000000, 8));
    CHECK(!pending.MayOverlap(0x100100, 8));
}

// Random Add and Remove against a model, with every query checked against the exact model.
void TestAgainstModel(std::uint32_t seed) {
    std::mt19937_64 rng{seed};
    constexpr std::uintptr_t Space = std::uintptr_t{8} << 20; // 8 MB: granules alias here
    struct Model {
        std::uint64_t id;
        std::vector<Range> copies;
    };
    PendingBackingCopies pending;
    std::vector<Model> model;
    std::uint64_t next_id = 1;
    const auto random_copies = [&] {
        std::vector<Range> copies;
        const auto n = 1 + rng() % 4;
        for (std::uint64_t i = 0; i < n; ++i) {
            const std::uintptr_t start = rng() % Space;
            const std::uintptr_t size = rng() % 200 == 0 ? (rng() % (3u << 20)) : (rng() % 3000);
            if (rng() % 40 == 0) {
                copies.push_back({start + 16, start}); // Inverted: never overlaps.
            } else {
                copies.push_back({start, std::min(Space, start + size)});
            }
        }
        return copies;
    };
    const auto random_query = [&](std::uintptr_t& address, std::uint64_t& size) {
        switch (rng() % 12) {
        case 0: // Wraps past the end of the address space.
            address = Max - rng() % 16;
            size = 1 + rng() % 32;
            break;
        case 1:
            address = rng() % Space;
            size = 0;
            break;
        case 2: // Too wide for the filter.
            address = rng() % Space;
            size = 1 + rng() % (1u << 16);
            break;
        default: // Header sized.
            address = rng() % Space;
            size = 1 + rng() % 48;
            break;
        }
    };

    for (int step = 0; step < 2000; ++step) {
        const auto op = rng() % 5;
        if (op < 2 || model.empty()) {
            Model entry{next_id++, random_copies()};
            pending.Add(entry.id, entry.copies);
            model.push_back(std::move(entry));
        } else if (op == 2) {
            const auto index = rng() % model.size();
            pending.Remove(model[index].id);
            model.erase(model.begin() + static_cast<std::ptrdiff_t>(index));
        } else if (op == 3) {
            pending.Remove(next_id + 1000); // Never added.
        }
        for (int q = 0; q < 16; ++q) {
            std::uintptr_t address{};
            std::uint64_t size{};
            random_query(address, size);
            bool exact = false;
            for (const auto& entry : model) {
                exact |= VideoCore::ReadbackCopiesOverlap(entry.copies, address, size);
            }
            CHECK(pending.Overlaps(address, size) == exact);
            const bool may = pending.MayOverlap(address, size);
            if (exact) {
                CHECK(may); // No false negatives.
            }
            if (!may) {
                CHECK(!exact);
            }
        }
    }
    for (const auto& entry : model) {
        pending.Remove(entry.id);
    }
    // Nothing is left: no granule may report a pending copy.
    for (std::uintptr_t address = 0; address < Space; address += GranuleBytes) {
        CHECK(!pending.MayOverlap(address, GranuleBytes));
    }
}

// The reader (this thread) plays the command processor: it adds records and queries MayOverlap
// without the lock, then checks the exact answer under the lock. The worker plays the download
// callbacks: it writes the record's bytes back to "guest memory", then removes the record under the
// lock.
void TestConcurrent(std::uint32_t seed, int rounds) {
    constexpr std::uintptr_t Space = std::uintptr_t{8} << 20;
    constexpr std::size_t Granules = Space >> Shift;
    constexpr std::size_t MaxBacklog = 256;
    PendingBackingCopies pending;
    std::mutex mutex; // Stands in for pending_unmarks_mutex.
    // "Guest memory": the id of the last record written back to each granule.
    std::vector<std::atomic<std::uint64_t>> guest(Granules);
    for (auto& value : guest) {
        value.store(0, std::memory_order_relaxed);
    }
    // Only the reader writes this: the id of the last record added to each granule.
    std::vector<std::uint64_t> expected(Granules, 0);

    struct Job {
        std::uint64_t id;
        std::vector<Range> copies;
    };
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::deque<Job> queue;
    bool closed = false;
    std::atomic<std::uint64_t> started{0}; // Id of the record the worker is handling.

    std::thread worker([&] {
        for (;;) {
            Job job;
            {
                std::unique_lock lk{queue_mutex};
                queue_cv.wait(lk, [&] { return closed || !queue.empty(); });
                if (queue.empty()) {
                    return;
                }
                job = std::move(queue.front());
                queue.pop_front();
            }
            started.store(job.id, std::memory_order_relaxed);
            for (const auto& [start, end] : job.copies) {
                if (start >= end) {
                    continue;
                }
                for (std::uintptr_t g = start >> Shift; g <= (end - 1) >> Shift; ++g) {
                    guest[g].store(job.id, std::memory_order_relaxed);
                }
            }
            std::scoped_lock lk{mutex};
            pending.Remove(job.id);
        }
    });

    std::mt19937_64 rng{seed};
    const auto random_copies = [&] {
        std::vector<Range> copies;
        const auto n = 1 + rng() % 3;
        for (std::uint64_t i = 0; i < n; ++i) {
            const std::uintptr_t start = rng() % Space;
            const std::uintptr_t size =
                rng() % 60 == 0 ? (rng() % (3u << 20)) : (rng() % (64u << 10));
            copies.push_back({start, std::min(Space, start + size)});
        }
        return copies;
    };
    std::uint64_t next_id = 1;
    std::vector<std::vector<Range>> copies_of; // Copies of every record added so far (reader only).
    for (int round = 0; round < rounds; ++round) {
        if (rng() % 3 != 0) {
            Job job{next_id++, random_copies()};
            copies_of.push_back(job.copies);
            {
                std::scoped_lock lk{mutex};
                pending.Add(job.id, job.copies);
            }
            for (const auto& [start, end] : job.copies) {
                if (start >= end) {
                    continue;
                }
                for (std::uintptr_t g = start >> Shift; g <= (end - 1) >> Shift; ++g) {
                    expected[g] = job.id;
                }
            }
            {
                std::scoped_lock lk{queue_mutex};
                queue.push_back(std::move(job));
            }
            queue_cv.notify_one();
            // Keep the backlog short so the locked checks below stay cheap.
            for (;;) {
                {
                    std::scoped_lock lk{queue_mutex};
                    if (queue.size() < MaxBacklog) {
                        break;
                    }
                }
                std::this_thread::yield();
            }
        }
        for (int q = 0; q < 8; ++q) {
            std::uintptr_t address = rng() % (Space - 8192);
            std::uint64_t size = 1 + rng() % (rng() % 16 == 0 ? 8192 : 48);
            // Half the queries go to the record the worker is handling: its write back and removal
            // are the window this test is about.
            const std::uint64_t id = started.load(std::memory_order_relaxed);
            if (q % 2 == 0 && id != 0) {
                const auto& copies = copies_of[id - 1];
                const Range r = copies[rng() % copies.size()];
                if (r.first < r.second) {
                    address = r.first + rng() % (r.second - r.first);
                    size = 1 + rng() % 48;
                }
            }
            size = std::min<std::uint64_t>(size, Space - address);
            if (!pending.MayOverlap(address, size)) {
                // Nothing pending covers these granules. So the last record added to each one has
                // been removed, and its write back came before the removal: the bytes show it.
                // Read them before taking the mutex, as the caller of IsCpuCopyCurrent does.
                for (std::uintptr_t g = address >> Shift; g <= (address + size - 1) >> Shift; ++g) {
                    CHECK(guest[g].load(std::memory_order_relaxed) == expected[g]);
                }
                std::scoped_lock lk{mutex};
                CHECK(!pending.Overlaps(address, size));
            }
            {
                std::scoped_lock lk{mutex};
                if (pending.Overlaps(address, size)) {
                    CHECK(pending.MayOverlap(address, size));
                }
            }
        }
    }

    {
        std::scoped_lock lk{queue_mutex};
        closed = true;
    }
    queue_cv.notify_all();
    worker.join();
    CHECK(queue.empty());
    for (std::uintptr_t address = 0; address < Space; address += GranuleBytes) {
        CHECK(!pending.MayOverlap(address, GranuleBytes));
    }
}

} // namespace

int main() {
    TestBasics();
    TestCounts();
    TestWide();
    for (std::uint32_t seed = 1; seed <= 4; ++seed) {
        TestAgainstModel(seed);
    }
    for (std::uint32_t seed = 1; seed <= 3; ++seed) {
        TestConcurrent(seed, 40000);
    }
    std::puts("pending backing filter: all checks passed");
    return 0;
}
