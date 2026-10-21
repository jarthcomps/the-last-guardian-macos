// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace VideoCore {
// Everything a shader image binding's lookup result depends on: the T# (after GetSharp), the
// ImageResource flags its ImageDesc reads (depth, array, written; the others are kept too), the
// mip fallback mode and index, the binding index inside a mip array, and the alt tile bit that
// ImageInfo takes from the T# and the Neo mode. The shader program is not an input: shaders that
// bind the same T# with the same flags resolve to the same image and view, so they share an entry.
struct TextureLookupKey {
    std::array<std::uint32_t, 8> descriptor{};
    std::uint32_t resource_flags{}, mip_mode{}, mip_index{}, binding_index{};
    bool alt_tile{};
    bool operator==(const TextureLookupKey&) const = default;
};

// Registration stamps of the texture cache's pages (page index = guest address >> 20). The stamp
// of a page is the sequence number of the last register or unregister of an image that covers
// it. A lookup takes Seq() when it starts and later asks UnchangedSince(): true when no image
// covering one of its pages was registered or unregistered after that.
class PageStamps {
public:
    static constexpr std::size_t BlockBits = 10;
    static constexpr std::size_t BlockPages = std::size_t{1} << BlockBits;
    static constexpr std::size_t NumBlocks = std::size_t{1} << BlockBits;

    [[nodiscard]] std::uint64_t Seq() const noexcept {
        return seq;
    }

    /// Records a register or unregister of an image over pages first..last (no page if last <
    /// first).
    void Bump(std::uint64_t first, std::uint64_t last) {
        const std::uint64_t stamp = ++seq;
        for (std::uint64_t page = first; page <= last; ++page) {
            Slot(page) = stamp;
        }
    }

    /// True when no change to pages first..last came after the sequence number `since`.
    [[nodiscard]] bool UnchangedSince(std::uint64_t first, std::uint64_t last,
                                      std::uint64_t since) const noexcept {
        for (std::uint64_t page = first; page <= last; ++page) {
            if (Stamp(page) > since) {
                return false;
            }
        }
        return true;
    }

private:
    // Pages past the table share one stamp. A change to any of them counts for all of them, which
    // is conservative.
    std::uint64_t& Slot(std::uint64_t page) {
        const std::uint64_t block = page >> BlockBits;
        if (block >= NumBlocks) {
            return overflow;
        }
        auto& values = blocks[block];
        if (!values) {
            values = std::make_unique<std::array<std::uint64_t, BlockPages>>();
        }
        return (*values)[page & (BlockPages - 1)];
    }
    [[nodiscard]] std::uint64_t Stamp(std::uint64_t page) const noexcept {
        const std::uint64_t block = page >> BlockBits;
        if (block >= NumBlocks) {
            return overflow;
        }
        const auto& values = blocks[block];
        return values ? (*values)[page & (BlockPages - 1)] : 0;
    }

    std::uint64_t seq{};
    std::uint64_t overflow{};
    std::array<std::unique_ptr<std::array<std::uint64_t, BlockPages>>, NumBlocks> blocks{};
};

// An image may be deleted once the GC tick is more than NumFramesBeforeRemoval past its last
// access (tick_accessed_last). An access sets tick_accessed_last to the current tick, which can
// only make an image younger.
inline constexpr std::uint64_t NumFramesBeforeRemoval = 32;
[[nodiscard]] inline bool AgedOut(std::uint64_t gc_tick,
                                  std::uint64_t tick_accessed_last) noexcept {
    return gc_tick - tick_accessed_last > NumFramesBeforeRemoval;
}
/// The first GC tick at which AgedOut holds for an image last accessed at tick_accessed_last.
/// AgedOut stays true for every later tick.
[[nodiscard]] inline std::uint64_t FirstAgedOutTick(std::uint64_t tick_accessed_last) noexcept {
    return tick_accessed_last + NumFramesBeforeRemoval + 1;
}

// What a texture lookup result depends on, recorded when the lookup starts (FindImageLocked).
//
// The lookup reads the images registered on the pages of its address range. Their geometry and
// format (the info FindImageLocked reads) never change after registration, so the only inputs
// that can change are the set of registered images on those pages and, while an overlap is being
// resolved, the GC age of the images it compares. The events that can change the result are:
//  1. An image covering one of the range's pages is registered or unregistered (RegisterImage,
//     UnregisterImage; every path that creates, frees or expands an image goes through them). The
//     page stamps see it. That covers each change to which images overlap the range, and to which
//     one the lookup picks (the last exact match in page order). Unrelated images on the same
//     pages also invalidate, which is conservative.
//  2. An image the lookup compares for an overlap becomes old enough to delete: a GC tick
//     (gc_tick advances once per guest submission). tick_limit is the first such tick. Only the
//     comparisons that consult the age set it. A lookup with an exact match, or one that compares
//     no image that way, has no limit. An access only makes an image younger, so the limit
//     recorded at the start stays a valid bound.
//  3. A lookup that reads binding.is_target of a compared image (the left-subresource case of
//     ResolveOverlap) is not reusable: the flag is set and cleared for every draw. The caller
//     checks needs_rebind on the cached image.
//  4. A lookup over an empty page range is not reusable: no page stamp would see its images.
// Nothing else is read. The key fixes the descriptor, and the other state of the cache (LRU
// order, image flags, memory tracking) does not change the result. A hit does the bookkeeping the
// full lookup does for the image it returns (its access time and its LRU position).
struct LookupValidity {
    static constexpr std::uint64_t NoTickLimit = std::numeric_limits<std::uint64_t>::max();

    std::uint64_t first_page{}, last_page{}; ///< pages of the requested address range
    std::uint64_t registry_seq{};            ///< PageStamps::Seq() when the lookup started
    std::uint64_t tick_limit = NoTickLimit;  ///< the result holds only for GC ticks below this
    bool reusable{true};                     ///< false: the result must not be reused
};

// The result of a lookup for one key, kept while its validity holds (see LookupValidity). A slot
// has one entry: a new result replaces the old one.
template <typename Key, typename Result>
class LookupReuseEntry {
public:
    /// The result for the same key, if it is still valid at this GC tick and registry state.
    [[nodiscard]] const Result* Find(const Key& requested, const PageStamps& stamps,
                                     std::uint64_t current_tick) const {
        if (!result || !(key == requested) || current_tick >= validity.tick_limit ||
            !stamps.UnchangedSince(validity.first_page, validity.last_page,
                                   validity.registry_seq)) {
            return nullptr;
        }
        return &*result;
    }
    /// Stores the result of a lookup that started with `valid`. A result that is not reusable is
    /// not kept.
    void Store(const Key& requested, const LookupValidity& valid, Result resolved) {
        key = requested;
        validity = valid;
        if (valid.reusable) {
            result = std::move(resolved);
        } else {
            result.reset();
        }
    }
    void Invalidate() {
        result.reset();
    }

private:
    Key key{};
    LookupValidity validity{};
    std::optional<Result> result;
};

// The value decoded from a lookup key's inputs (the ImageDesc of a T#). The decode depends on
// nothing but the key, so when a lookup result goes stale and the key is the same, the stored value
// is returned instead of decoding the T# again. Get returns a copy: the caller changes its copy
// (the mip fallback) and never the stored value.
template <typename Key, typename Value>
class DecodeReuseEntry {
public:
    template <typename Decode>
    Value Get(const Key& requested, Decode&& decode) {
        if (!value || !(key == requested)) {
            value.emplace(decode());
            key = requested;
        }
        return *value;
    }

private:
    Key key{};
    std::optional<Value> value;
};
} // namespace VideoCore
