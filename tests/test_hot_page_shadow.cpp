// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Tests of the hot page shadows (SHADPS4_HOT_PAGE_COMPARE).
//
// The decisions come from DecideHotShadow, and the region state from the real RegionManager. A
// model guest memory and a model buffer run the sequence of SynchronizeBuffer: reads, writes with
// their readback, texel reads, buffer joins, and decays. After every step:
//   - the buffer holds the guest bytes of every range a read covers (what the draw reads), and
//   - every shadow of the current buffer equals the buffer bytes of its page.
// The state machine of the decisions and the shadow limit are also checked on their own.

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

#include "video_core/buffer_cache/hot_page_shadow.h"
#include "video_core/buffer_cache/region_manager.h"

namespace VideoCore {
struct PageManager::Impl {};

PageManager::PageManager(Vulkan::Rasterizer*) : impl{std::make_unique<Impl>()} {}
PageManager::~PageManager() = default;

template <>
void PageManager::UpdatePageWatchersForRegion<true, false>(VAddr, RegionBits&) const {}
template <>
void PageManager::UpdatePageWatchersForRegion<false, false>(VAddr, RegionBits&) const {}
} // namespace VideoCore

namespace {

using namespace VideoCore;

constexpr VAddr CpuBase = 0x10000000; // 4 MB aligned: the start of a region
constexpr size_t NumPages = 16;
constexpr size_t PageBytes = TRACKER_BYTES_PER_PAGE;
constexpr size_t RangeBytes = NumPages * PageBytes;

bool Fail(const char* what, u64 seed, u64 step) {
    std::printf("FAIL %s (seed %llu, step %llu)\n", what, static_cast<unsigned long long>(seed),
                static_cast<unsigned long long>(step));
    return false;
}

// A buffer cache in miniature: one buffer covering NumPages pages, with its guest memory.
class Model {
public:
    Model(u64 seed, size_t max_armed)
        : rng{seed}, manager{&tracker, CpuBase}, seed{seed} {
        manager.SetHotPagesEnabled(true);
        manager.SetShadowLedger(&ledger);
        ledger.max_armed = max_armed;
        guest.fill(0);
        gpu.fill(0);
    }

    bool Run(u64 steps) {
        for (u64 step = 0; step < steps; ++step) {
            const u64 pick = rng() % 100;
            if (pick < 40) {
                GuestWrite();
            } else if (pick < 75) {
                if (!Read(step, /*texel=*/false)) {
                    return false;
                }
            } else if (pick < 80) {
                if (!GpuWrite(step)) {
                    return false;
                }
            } else if (pick < 88) {
                TexelRead();
            } else if (pick < 91) {
                serial = serial == 0xFFFFFFFFu ? 1 : serial + 1; // joined: its bytes are copied
            } else if (pick < 93) {
                Recreate(); // deleted and created again: its bytes are not copied
            } else if (pick < 95) {
                manager.DecayHotPages();
                manager.ClearShadows();
            } else {
                if (!Read(step, /*texel=*/true)) {
                    return false;
                }
            }
            if (!CheckShadows(step)) {
                return false;
            }
        }
        return true;
    }

    HotShadowLedger& Ledger() {
        return ledger;
    }

private:
    // A CPU write. A write to a protected page faults, as in InvalidateRegion.
    void GuestWrite() {
        const size_t page = rng() % NumPages;
        if (rng() % 5 == 0) {
            // A whole page of one of four patterns: the page often returns to an earlier value,
            // which is the case a shadow must not outlive.
            const u8 pattern = static_cast<u8>(rng() % 4);
            std::memset(&guest[page * PageBytes], pattern, PageBytes);
        } else {
            const size_t off = rng() % (PageBytes - 64);
            const size_t len = 1 + rng() % 64;
            for (size_t i = 0; i < len; ++i) {
                guest[page * PageBytes + off + i] = static_cast<u8>(rng());
            }
        }
        const VAddr page_addr = CpuBase + page * PageBytes;
        if (!manager.IsRegionModified<Type::CPU>(page * PageBytes, PageBytes)) {
            manager.NoteCpuWrite(page_addr, PageBytes);
            manager.ChangeRegionState<Type::CPU, true>(page_addr, PageBytes);
        }
    }

    // A random byte range of the buffer, up to 8 pages.
    void RandomRange(VAddr& addr, u64& size) {
        const u64 offset = rng() % RangeBytes;
        const u64 max_size = std::min<u64>(RangeBytes - offset, 8 * PageBytes);
        size = 1 + rng() % max_size;
        addr = CpuBase + offset;
    }

    // SynchronizeBuffer for a read (texel or not) of a random range: the upload pass, the
    // decisions for the hot pages, the copies with their snapshots, then the draw's check.
    bool Read(u64 step, bool texel) {
        VAddr addr;
        u64 size;
        RandomRange(addr, size);
        Sync(addr, size, /*is_written=*/false, texel);
        if (!texel) {
            for (u64 i = addr - CpuBase; i < addr - CpuBase + size; ++i) {
                if (gpu[i] != guest[i]) {
                    return Fail("a read sees bytes other than the guest's", seed, step);
                }
            }
        }
        return true;
    }

    // A GPU write of a range: the buffer is written, then read back into guest memory.
    bool GpuWrite(u64 step) {
        VAddr addr;
        u64 size;
        RandomRange(addr, size);
        Sync(addr, size, /*is_written=*/true, /*texel=*/false);
        const u64 sub_offset = rng() % size;
        const u64 sub_size = 1 + rng() % (size - sub_offset);
        const u64 base = addr - CpuBase;
        for (u64 i = base + sub_offset; i < base + sub_offset + sub_size; ++i) {
            gpu[i] = static_cast<u8>(rng());
            guest[i] = gpu[i]; // the readback
        }
        for (u64 i = base; i < base + size; ++i) {
            if (gpu[i] != guest[i]) {
                return Fail("a readback differs from the buffer", seed, step);
            }
        }
        return true;
    }

    // A buffer deleted and created again over the same range: the new buffer holds no bytes yet,
    // and the unmap marked the range CPU modified, so the next read copies it. The shadows of the
    // deleted buffer are not forgotten: their serial is what keeps them from being trusted.
    void Recreate() {
        serial = serial == 0xFFFFFFFFu ? 1 : serial + 1;
        for (size_t i = 0; i < RangeBytes; ++i) {
            gpu[i] = static_cast<u8>(rng());
        }
        manager.ChangeRegionState<Type::CPU, true>(CpuBase, RangeBytes);
    }

    // An image alias of the buffer: the buffer gets tiled data that is not the guest bytes, so the
    // pages are marked dirty and the next read copies them again.
    void TexelRead() {
        VAddr addr;
        u64 size;
        RandomRange(addr, size);
        Sync(addr, size, /*is_written=*/false, /*texel=*/true);
        const u64 base = addr - CpuBase;
        const u64 victim = base + rng() % size;
        gpu[victim] = static_cast<u8>(gpu[victim] + 1);
        const VAddr page_addr = CpuBase + (victim / PageBytes) * PageBytes;
        manager.ChangeRegionState<Type::CPU, true>(page_addr, PageBytes);
    }

    // Mirrors SynchronizeBuffer. A write or texel read forgets the shadows of its range first; the
    // hot pages are decided after the upload pass, and the copies take their snapshots.
    void Sync(VAddr addr, u64 size, bool is_written, bool texel) {
        if (is_written || texel) {
            manager.ForgetShadows(addr, size);
        }
        const bool compare_hot = !is_written && !texel;
        std::vector<std::pair<VAddr, u64>> runs;
        std::vector<HotPageRef> refs;
        manager.ForEachModifiedRange<Type::CPU, true>(
            addr, static_cast<s64>(size),
            [&](VAddr run_addr, u64 run_size) {
                runs.emplace_back(run_addr, run_size);
                manager.ForgetShadows(run_addr, run_size);
            },
            /*keep_hot=*/!is_written, compare_hot ? &refs : nullptr);

        struct Snapshot {
            VAddr page;
            HotShadow* shadow;
            u32 serial;
        };
        std::vector<Snapshot> snapshots;
        if (compare_hot) {
            // The shadows of this buffer are compared with the guest memory in one pass, as
            // BufferCache does; the decisions then take the results in the same order.
            struct Match {
                VAddr source;
                const u8* expected;
                bool equal;
            };
            std::vector<Match> matches;
            for (const HotPageRef& ref : refs) {
                const HotShadow& shadow = ref.manager->ShadowAt(ref.page);
                if (shadow.data && shadow.serial == serial) {
                    matches.push_back({ref.page, shadow.data->data(), false});
                }
            }
            for (Match& match : matches) {
                match.equal = std::memcmp(&guest[match.source - CpuBase], match.expected,
                                          PageBytes) == 0;
            }
            size_t next_match = 0;
            bool open = false;
            VAddr run_start = 0;
            u64 run_size = 0;
            const auto close_run = [&] {
                if (open) {
                    runs.emplace_back(run_start, run_size);
                    open = false;
                }
            };
            for (const HotPageRef& ref : refs) {
                HotShadow& shadow = ref.manager->ShadowAt(ref.page);
                const HotShadowDecision decision = DecideHotShadow(
                    shadow, serial, ledger, [&](const u8* bytes) {
                        const Match& match = matches.at(next_match++);
                        return match.source == ref.page && match.expected == bytes &&
                               match.equal;
                    });
                if (decision.action == HotShadowAction::Skip) {
                    close_run();
                    continue;
                }
                if (!open || run_start + run_size != ref.page) {
                    close_run();
                    open = true;
                    run_start = ref.page;
                    run_size = 0;
                }
                run_size += PageBytes;
                if (decision.action == HotShadowAction::CopyAndCommit) {
                    snapshots.push_back({ref.page, &shadow, serial});
                }
            }
            close_run();
        }

        // The copies stage the guest bytes; the snapshots keep the staged bytes of their pages.
        for (const auto& [run_addr, run_size] : runs) {
            std::memcpy(&gpu[run_addr - CpuBase], &guest[run_addr - CpuBase], run_size);
        }
        for (const Snapshot& snapshot : snapshots) {
            std::memcpy(snapshot.shadow->data->data(), &guest[snapshot.page - CpuBase],
                        PageBytes);
            CommitHotShadow(*snapshot.shadow, snapshot.serial, ledger);
        }
    }

    // Every shadow of the current buffer equals its page in the buffer, and the number of kept
    // shadows is the ledger's count.
    bool CheckShadows(u64 step) {
        size_t kept = 0;
        for (size_t page = 0; page < 1024; ++page) {
            const HotShadow& shadow = manager.ShadowAt(CpuBase + page * PageBytes);
            if (!shadow.data) {
                continue;
            }
            ++kept;
            if (page < NumPages && shadow.serial == serial &&
                std::memcmp(&gpu[page * PageBytes], shadow.data->data(), PageBytes) != 0) {
                return Fail("a shadow of the buffer differs from the buffer", seed, step);
            }
        }
        if (kept != ledger.armed) {
            return Fail("the shadow count differs from the ledger", seed, step);
        }
        if (ledger.armed > ledger.max_armed) {
            return Fail("more shadows than the limit", seed, step);
        }
        return true;
    }

    std::mt19937_64 rng;
    PageManager tracker{nullptr};
    RegionManager manager;
    HotShadowLedger ledger{};
    u64 seed;
    std::array<u8, RangeBytes> guest{};
    std::array<u8, RangeBytes> gpu{};
    u32 serial = 1;
};

// The state machine of one page, step by step.
bool TestDecisionStates() {
    HotShadowLedger ledger{};
    HotShadow shadow;
    const auto same = [](const u8*) { return true; };
    const auto changed = [](const u8*) { return false; };

    // Nothing kept yet: copy and keep a snapshot.
    auto d = DecideHotShadow(shadow, 1, ledger, same);
    if (d.action != HotShadowAction::CopyAndCommit || !d.snapshot || ledger.armed != 1) {
        return false;
    }
    // Not committed yet: the shadow is never trusted.
    if (shadow.serial != 0) return false;
    CommitHotShadow(shadow, 1, ledger);

    // Unchanged: skip the copy.
    d = DecideHotShadow(shadow, 1, ledger, same);
    if (d.action != HotShadowAction::Skip || ledger.skips != 1) return false;

    // Changed once: copy and keep the new snapshot (score 3).
    d = DecideHotShadow(shadow, 1, ledger, changed);
    if (d.action != HotShadowAction::CopyAndCommit || shadow.score != 3) return false;
    CommitHotShadow(shadow, 1, ledger);

    // Unchanged: the score goes down, and the copy is skipped.
    d = DecideHotShadow(shadow, 1, ledger, same);
    if (d.action != HotShadowAction::Skip || shadow.score != 2) return false;

    // Changed again (score 2 + 3 = 5): disarmed, with a cooldown of reads that copy without a
    // shadow.
    d = DecideHotShadow(shadow, 1, ledger, changed);
    if (d.action != HotShadowAction::Copy || shadow.data || ledger.armed != 0 ||
        ledger.disarms != 1 || shadow.cooldown != HotShadowCooldownReads) {
        return false;
    }
    for (u32 read = 0; read < HotShadowCooldownReads; ++read) {
        if (DecideHotShadow(shadow, 1, ledger, same).action != HotShadowAction::Copy) return false;
    }
    if (shadow.cooldown != 0) return false;
    // After the cooldown the page may be kept again.
    if (DecideHotShadow(shadow, 1, ledger, same).action != HotShadowAction::CopyAndCommit ||
        ledger.armed != 1) {
        return false;
    }
    CommitHotShadow(shadow, 1, ledger);

    // Another buffer (serial): the shadow is not trusted, and the copy takes a new snapshot.
    d = DecideHotShadow(shadow, 2, ledger, same);
    if (d.action != HotShadowAction::CopyAndCommit || ledger.armed != 1) return false;
    CommitHotShadow(shadow, 2, ledger);
    if (DecideHotShadow(shadow, 2, ledger, same).action != HotShadowAction::Skip) return false;

    ReleaseHotShadow(shadow, ledger);
    return ledger.armed == 0 && shadow.serial == 0;
}

// The limit: a page past the limit is copied without a shadow, and counted as refused.
bool TestShadowLimit() {
    HotShadowLedger ledger{};
    ledger.max_armed = 1;
    HotShadow first;
    HotShadow second;
    if (DecideHotShadow(first, 1, ledger, [](const u8*) { return true; }).action !=
        HotShadowAction::CopyAndCommit) {
        return false;
    }
    CommitHotShadow(first, 1, ledger);
    if (DecideHotShadow(second, 1, ledger, [](const u8*) { return true; }).action !=
            HotShadowAction::Copy ||
        ledger.refused != 1 || second.data || ledger.armed != 1) {
        return false;
    }
    // The page that holds a shadow keeps it.
    return DecideHotShadow(first, 1, ledger, [](const u8*) { return true; }).action ==
           HotShadowAction::Skip;
}

// A write to a range drops the shadows of its pages, and only those.
bool TestForgetShadows() {
    PageManager tracker{nullptr};
    RegionManager manager(&tracker, CpuBase);
    HotShadowLedger ledger{};
    manager.SetShadowLedger(&ledger);
    for (size_t page = 0; page < 4; ++page) {
        HotShadow& shadow = manager.ShadowAt(CpuBase + page * PageBytes);
        DecideHotShadow(shadow, 1, ledger, [](const u8*) { return true; });
        CommitHotShadow(shadow, 1, ledger);
    }
    manager.ForgetShadows(CpuBase + 1 * PageBytes, 2 * PageBytes);
    return ledger.armed == 2 && manager.ShadowAt(CpuBase).data != nullptr &&
           manager.ShadowAt(CpuBase + 1 * PageBytes).data == nullptr &&
           manager.ShadowAt(CpuBase + 2 * PageBytes).data == nullptr &&
           manager.ShadowAt(CpuBase + 3 * PageBytes).data != nullptr;
}

} // namespace

int main() {
    if (!TestDecisionStates()) return 1;
    if (!TestShadowLimit()) return 2;
    if (!TestForgetShadows()) return 3;

    constexpr u64 Seeds = 96;
    constexpr u64 Steps = 3000;
    u64 skips = 0;
    u64 disarms = 0;
    u64 refused = 0;
    u64 commits = 0;
    for (u64 seed = 1; seed <= Seeds; ++seed) {
        // Most seeds have no limit in practice; some run with a few kept shadows, to exercise the
        // refusals.
        const size_t max_armed = seed % 4 == 0 ? 3 : 8192;
        Model model{seed, max_armed};
        if (!model.Run(Steps)) return 4;
        skips += model.Ledger().skips;
        disarms += model.Ledger().disarms;
        refused += model.Ledger().refused;
        commits += model.Ledger().commits;
    }
    // The run must reach the paths it is about, or it proves nothing about them.
    if (skips == 0 || disarms == 0 || refused == 0 || commits == 0) {
        std::printf("FAIL coverage: skips %llu disarms %llu refused %llu commits %llu\n",
                    static_cast<unsigned long long>(skips),
                    static_cast<unsigned long long>(disarms),
                    static_cast<unsigned long long>(refused),
                    static_cast<unsigned long long>(commits));
        return 5;
    }
    std::printf("ok: skips %llu disarms %llu refused %llu commits %llu\n",
                static_cast<unsigned long long>(skips), static_cast<unsigned long long>(disarms),
                static_cast<unsigned long long>(refused), static_cast<unsigned long long>(commits));
    return 0;
}
