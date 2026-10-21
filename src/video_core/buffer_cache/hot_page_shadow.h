// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <memory>

#include "common/types.h"
#include "video_core/buffer_cache/region_definitions.h"

namespace VideoCore {

class RegionManager;

/// A hot page that an upload pass left to its caller (see HotShadow). page is its CPU address.
struct HotPageRef {
    RegionManager* manager;
    VAddr page;
};

/// Compare-skip for hot pages (SHADPS4_HOT_PAGE_COMPARE=1).
///
/// A hot page stays CPU modified after each upload, so every read that covers it copies it again,
/// changed or not. A shadow keeps the bytes that were last copied into a buffer for the page. A
/// later read compares the guest bytes with the shadow, and when they are equal the copy is
/// skipped: the buffer already holds these bytes.
///
/// Invariant (what makes a skip safe): a shadow with serial s and data D means the buffer with
/// serial s holds exactly D at that page. It holds because D is the staged copy of the page,
/// taken as the page was copied into that buffer (see CommitHotShadow), and because every other
/// write to the buffer's memory forgets the shadow first (ForgetHotShadows): a copy that keeps no
/// snapshot, a write upload or GPU write, a texel read (its image copy writes the buffer memory),
/// and a decay. A buffer that is replaced by a join or a new buffer has another serial. The buffer
/// changes only by copies and GPU writes, so when the guest bytes equal D at the check, the copy
/// would have written D, and the buffer already holds D for the draw that reads it.
///
/// The state is kept per page in RegionManager and used by the command processor thread only. The
/// compare runs outside the region locks, so it never nests the memory lock inside a region lock.
struct HotShadow {
    u32 serial{};   ///< Serial of the buffer holding data (0: none, or not committed yet).
    u32 score{};    ///< Changed compares raise it, unchanged compares lower it.
    u32 cooldown{}; ///< Reads left before a disarmed page may get a shadow again.
    std::unique_ptr<std::array<u8, TRACKER_BYTES_PER_PAGE>> data; ///< Null when not kept.
};

/// Shared limit and counters for all shadows (command processor thread only).
struct HotShadowLedger {
    size_t armed{};          ///< Pages with data.
    size_t max_armed{8192};  ///< Pages with data at once; more hot pages are copied as before.
    u64 checks{};            ///< Compares of a kept shadow with the guest bytes.
    u64 skips{};             ///< Compares that were equal, so the copy was skipped.
    u64 changed{};           ///< Compares that found the page changed.
    u64 commits{};           ///< Shadows taken from a copy.
    u64 disarms{};           ///< Pages dropped after repeated changes.
    u64 refused{};           ///< Hot pages not kept because max_armed was reached.
};

enum class HotShadowAction : u8 {
    Skip,           ///< Equal to the buffer: no copy.
    Copy,           ///< Copy; keep no shadow for this copy.
    CopyAndCommit,  ///< Copy, then store the staged bytes into snapshot and commit.
};

struct HotShadowDecision {
    HotShadowAction action{HotShadowAction::Copy};
    u8* snapshot{}; ///< For CopyAndCommit: where the staged page goes.
};

/// A changed compare adds this score; an unchanged one subtracts one.
inline constexpr u32 HotShadowChangedPenalty = 3;
/// A score at or above this disarms the page: its shadow is dropped (changes that often make a
/// shadow cost more than it saves: a changed page pays the compare and the snapshot).
inline constexpr u32 HotShadowDisarmScore = 4;
/// Reads that copy a disarmed page without a shadow before it may get one again.
inline constexpr u32 HotShadowCooldownReads = 64;

/// Drops the data of a shadow. The page is no longer known to be in the buffer.
inline void ReleaseHotShadow(HotShadow& shadow, HotShadowLedger& ledger) noexcept {
    if (shadow.data) {
        shadow.data.reset();
        --ledger.armed;
    }
    shadow.serial = 0;
}

/// Decides how a hot page is copied into the buffer with the given serial. equal(bytes) compares
/// the guest page with the shadow data and returns true when they are identical. Called before
/// the copies of the read are made, so the decision is for the bytes the copy would have staged.
template <typename Equal>
HotShadowDecision DecideHotShadow(HotShadow& shadow, u32 serial, HotShadowLedger& ledger,
                                  Equal&& equal) {
    if (shadow.data && shadow.serial == serial) {
        ++ledger.checks;
        if (equal(shadow.data->data())) {
            ++ledger.skips;
            shadow.score = shadow.score > 0 ? shadow.score - 1 : 0;
            return {HotShadowAction::Skip, nullptr};
        }
        ++ledger.changed;
        shadow.score += HotShadowChangedPenalty;
        if (shadow.score < HotShadowDisarmScore) {
            // The buffer no longer holds the shadow until the snapshot is committed.
            shadow.serial = 0;
            return {HotShadowAction::CopyAndCommit, shadow.data->data()};
        }
        ++ledger.disarms;
        ReleaseHotShadow(shadow, ledger);
        shadow.score = 0;
        shadow.cooldown = HotShadowCooldownReads;
        return {HotShadowAction::Copy, nullptr};
    }
    // Not kept for this buffer. The copy replaces whatever the page held in it.
    if (shadow.cooldown > 0) {
        --shadow.cooldown;
        ReleaseHotShadow(shadow, ledger);
        return {HotShadowAction::Copy, nullptr};
    }
    if (!shadow.data) {
        if (ledger.armed >= ledger.max_armed) {
            ++ledger.refused;
            shadow.serial = 0;
            return {HotShadowAction::Copy, nullptr};
        }
        shadow.data = std::make_unique<std::array<u8, TRACKER_BYTES_PER_PAGE>>();
        ++ledger.armed;
    }
    // Serial 0 until the snapshot is committed: a shadow that is not committed is never trusted.
    shadow.serial = 0;
    return {HotShadowAction::CopyAndCommit, shadow.data->data()};
}

/// Marks the shadow as holding the staged bytes of the page in the buffer with serial. Called
/// after the snapshot has been copied from the staging memory.
inline void CommitHotShadow(HotShadow& shadow, u32 serial, HotShadowLedger& ledger) noexcept {
    if (shadow.data) {
        shadow.serial = serial;
        ++ledger.commits;
    }
}

} // namespace VideoCore
