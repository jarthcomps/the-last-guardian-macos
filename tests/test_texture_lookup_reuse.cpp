// Texture lookup reuse (video_core/texture_cache/lookup_reuse.h). The key holds every input of a
// lookup. A result is kept while its validity holds: it is reusable, no image covering one of its
// pages was registered or unregistered after it started (page stamps), and the GC tick is below
// its limit.
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "video_core/texture_cache/lookup_reuse.h"

using VideoCore::AgedOut;
using VideoCore::FirstAgedOutTick;
using VideoCore::LookupReuseEntry;
using VideoCore::LookupValidity;
using VideoCore::NumFramesBeforeRemoval;
using VideoCore::PageStamps;
using VideoCore::TextureLookupKey;

namespace {

// The texture cache's page size (TextureCache::Traits::PageBits).
constexpr std::uint64_t PageBits = 20;

struct Pages {
    std::uint64_t first, last;
};

// The pages of an address range, as TextureCache::PageSpan computes them.
Pages PagesOf(std::uint64_t addr, std::uint64_t size) {
    return {addr >> PageBits, (addr + size - 1) >> PageBits};
}

// Image::Overlaps: the byte ranges [addr, addr + size) and [lo, lo + len) share a byte.
bool Overlaps(std::uint64_t addr, std::uint64_t size, std::uint64_t lo, std::uint64_t len) {
    return addr < lo + len && lo < addr + size;
}

TextureLookupKey BaseKey() {
    TextureLookupKey key{};
    key.descriptor = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    key.resource_flags = 0x8;
    key.mip_mode = 1;
    key.mip_index = 2;
    key.binding_index = 3;
    key.alt_tile = true;
    return key;
}

} // namespace

int main() {
    {
        // Each input of the lookup is part of the key.
        const TextureLookupKey base = BaseKey();
        assert(base == BaseKey());

        TextureLookupKey key = base;
        key.descriptor[7] ^= 1;
        assert(!(key == base));

        key = base;
        key.resource_flags ^= 1;
        assert(!(key == base));

        key = base;
        key.mip_mode ^= 1;
        assert(!(key == base));

        key = base;
        key.mip_index ^= 1;
        assert(!(key == base));

        key = base;
        key.binding_index ^= 1;
        assert(!(key == base));

        key = base;
        key.alt_tile = !key.alt_tile;
        assert(!(key == base));
    }
    {
        // Page stamps: a change is seen by the pages it covers, and only by a sequence number
        // taken before it.
        PageStamps stamps;
        assert(stamps.Seq() == 0);
        assert(stamps.UnchangedSince(0, 40, 0));

        stamps.Bump(5, 7);
        assert(stamps.Seq() == 1);
        assert(!stamps.UnchangedSince(0, 5, 0));
        assert(!stamps.UnchangedSince(6, 6, 0));
        assert(stamps.UnchangedSince(8, 9, 0));
        assert(stamps.UnchangedSince(0, 4, 0));
        assert(stamps.UnchangedSince(6, 7, stamps.Seq()));

        const std::uint64_t since = stamps.Seq();
        stamps.Bump(7, 7);
        assert(!stamps.UnchangedSince(6, 7, since));
        assert(stamps.UnchangedSince(5, 6, since));

        // Pages on both sides of the boundary between two stamp blocks (1024 pages each).
        stamps.Bump(1023, 1024);
        assert(!stamps.UnchangedSince(1023, 1023, 0));
        assert(!stamps.UnchangedSince(1024, 1024, 0));
        assert(stamps.UnchangedSince(1025, 1025, 0));

        // Pages past the table share one stamp: a change to one counts for all of them.
        const std::uint64_t table_end = std::uint64_t{1} << 20;
        const std::uint64_t before_overflow = stamps.Seq();
        stamps.Bump(table_end + 3, table_end + 3);
        assert(!stamps.UnchangedSince(table_end + 9, table_end + 9, before_overflow));
        assert(stamps.UnchangedSince(table_end - 1, table_end - 1, before_overflow));

        // An empty range (last < first) has no page that a change could cover.
        assert(stamps.UnchangedSince(9, 8, 0));
        const std::uint64_t before_empty = stamps.Seq();
        stamps.Bump(9, 8);
        assert(stamps.Seq() == before_empty + 1);
        assert(stamps.UnchangedSince(9, 8, before_empty));
    }
    {
        // GC age: an image is old enough to delete from FirstAgedOutTick on, and not before.
        for (const std::uint64_t accessed :
             {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{7}, std::uint64_t{100}}) {
            const std::uint64_t first = FirstAgedOutTick(accessed);
            assert(first == accessed + NumFramesBeforeRemoval + 1);
            for (std::uint64_t gc = accessed; gc < accessed + 3 * NumFramesBeforeRemoval; ++gc) {
                assert(AgedOut(gc, accessed) == (gc >= first));
            }
        }
        // An access sets the last access to the current tick, which makes the image younger.
        assert(!AgedOut(40, 40));
        assert(!AgedOut(40 + NumFramesBeforeRemoval, 40));
        assert(AgedOut(40 + NumFramesBeforeRemoval + 1, 40));
    }
    {
        // A result is found for the same key, while its validity holds, and for nothing else.
        PageStamps stamps;
        LookupReuseEntry<TextureLookupKey, int> entry;
        const TextureLookupKey key = BaseKey();
        assert(entry.Find(key, stamps, 0) == nullptr);

        LookupValidity valid;
        valid.first_page = 10;
        valid.last_page = 11;
        valid.registry_seq = stamps.Seq();
        valid.tick_limit = 50;
        entry.Store(key, valid, 42);
        const int* hit = entry.Find(key, stamps, 49);
        assert(hit && *hit == 42);
        // The GC tick reaches the limit: an image the lookup compared may now be deleted.
        assert(entry.Find(key, stamps, 50) == nullptr);

        // A registration or removal over another page does not change the result.
        stamps.Bump(12, 12);
        assert(entry.Find(key, stamps, 49));
        // One over a page of the range does.
        stamps.Bump(11, 11);
        assert(entry.Find(key, stamps, 49) == nullptr);

        // Another key misses, and a new result replaces the old one (one entry per slot).
        LookupValidity fresh = valid;
        fresh.registry_seq = stamps.Seq();
        fresh.tick_limit = 60;
        TextureLookupKey other = key;
        other.binding_index = 4;
        entry.Store(other, fresh, 43);
        assert(entry.Find(key, stamps, 10) == nullptr);
        const int* replaced = entry.Find(other, stamps, 10);
        assert(replaced && *replaced == 43);

        // A result without a limit holds for any tick.
        LookupValidity open = fresh;
        open.tick_limit = LookupValidity::NoTickLimit;
        entry.Store(key, open, 44);
        assert(entry.Find(key, stamps, ~std::uint64_t{0} - 1));

        // A result that is not reusable is never found.
        LookupValidity once = open;
        once.reusable = false;
        entry.Store(key, once, 45);
        assert(entry.Find(key, stamps, 0) == nullptr);

        entry.Store(key, open, 46);
        entry.Invalidate();
        assert(entry.Find(key, stamps, 0) == nullptr);
    }
    {
        // Randomized check against a model of the registry and of the GC. A stored result is found
        // exactly when its key is the same, it is reusable, the GC tick is below its limit, and no
        // register or unregister over one of its pages came after it was stored. A found result is
        // the stored one, the images that overlap its range are still the ones it was stored with,
        // and none of them has become old enough to delete.
        struct Image {
            std::uint64_t addr, size, accessed;
            bool alive;
        };
        struct Stored {
            bool valid{};
            TextureLookupKey key{};
            std::uint64_t addr{}, size{};
            Pages pages{};
            std::size_t change_index{};
            std::uint64_t tick_limit{};
            bool reusable{};
            int result{};
            std::vector<std::size_t> compared; // the images overlapping the range when stored
        };
        constexpr std::size_t NumSlots = 6;
        constexpr std::uint64_t MB = std::uint64_t{1} << 20;

        std::mt19937_64 rng{0x7e57};
        const auto below = [&](std::uint64_t n) { return rng() % n; };

        PageStamps stamps;
        LookupReuseEntry<TextureLookupKey, int> entries[NumSlots];
        Stored stored[NumSlots];
        std::vector<Image> images;
        std::vector<Pages> changes;
        std::uint64_t tick = 0;
        std::size_t found = 0, missed_changed = 0, missed_tick = 0, missed_other = 0;

        TextureLookupKey keys[3] = {BaseKey(), BaseKey(), BaseKey()};
        keys[1].binding_index = 4;
        keys[2].resource_flags = 9;

        const auto change_registry = [&](std::uint64_t addr, std::uint64_t size) {
            const Pages pages = PagesOf(addr, size);
            stamps.Bump(pages.first, pages.last);
            changes.push_back(pages);
        };

        for (int step = 0; step < 20000; ++step) {
            const std::uint64_t roll = below(100);
            if (roll < 3) {
                // Register an image.
                const std::uint64_t addr = 1 + below(30 * MB);
                const std::uint64_t size = 1 + below(2 * MB);
                images.push_back({addr, size, tick, true});
                change_registry(addr, size);
            } else if (roll < 6) {
                // Unregister an image.
                if (!images.empty()) {
                    Image& image = images[below(images.size())];
                    if (image.alive) {
                        image.alive = false;
                        change_registry(image.addr, image.size);
                    }
                }
            } else if (roll < 10) {
                // Access an image: it becomes younger.
                if (!images.empty()) {
                    Image& image = images[below(images.size())];
                    if (image.alive) {
                        image.accessed = tick;
                    }
                }
            } else if (roll < 12) {
                ++tick;
            } else if (roll < 40) {
                // Store a lookup result, with the validity TextureCache::FindImageLocked records.
                const std::size_t slot = below(NumSlots);
                const TextureLookupKey& key = keys[below(3)];
                const std::uint64_t addr = 1 + below(30 * MB);
                const std::uint64_t size = 1 + below(2 * MB);
                const Pages pages = PagesOf(addr, size);
                LookupValidity validity;
                validity.first_page = pages.first;
                validity.last_page = pages.last;
                validity.registry_seq = stamps.Seq();
                validity.reusable = below(20) != 0;
                Stored& s = stored[slot];
                s.compared.clear();
                for (std::size_t i = 0; i < images.size(); ++i) {
                    const Image& image = images[i];
                    if (image.alive && Overlaps(image.addr, image.size, addr, size)) {
                        s.compared.push_back(i);
                        validity.tick_limit =
                            std::min(validity.tick_limit, FirstAgedOutTick(image.accessed));
                    }
                }
                s.valid = true;
                s.key = key;
                s.addr = addr;
                s.size = size;
                s.pages = pages;
                s.change_index = changes.size();
                s.tick_limit = validity.tick_limit;
                s.reusable = validity.reusable;
                s.result = step;
                entries[slot].Store(key, validity, step);
            } else {
                // Find a result, mostly with the key it was stored with and sometimes with another.
                const std::size_t slot = below(NumSlots);
                const Stored& s = stored[slot];
                if (!s.valid) {
                    continue;
                }
                const TextureLookupKey& key = below(10) < 7 ? s.key : keys[below(3)];
                const int* hit = entries[slot].Find(key, stamps, tick);

                bool changed = false;
                for (std::size_t i = s.change_index; i < changes.size(); ++i) {
                    changed |= changes[i].first <= s.pages.last && s.pages.first <= changes[i].last;
                }
                const bool expect = key == s.key && s.reusable && tick < s.tick_limit && !changed;
                assert((hit != nullptr) == expect);
                if (!hit) {
                    if (key != s.key || !s.reusable) {
                        ++missed_other;
                    } else if (changed) {
                        ++missed_changed;
                    } else {
                        ++missed_tick;
                    }
                    continue;
                }
                ++found;
                assert(*hit == s.result);
                // The images that overlap the range now are the ones it was stored with, so
                // nothing was registered over it and no compared image was unregistered.
                std::vector<std::size_t> overlapping;
                for (std::size_t i = 0; i < images.size(); ++i) {
                    if (images[i].alive &&
                        Overlaps(images[i].addr, images[i].size, s.addr, s.size)) {
                        overlapping.push_back(i);
                    }
                }
                assert(overlapping == s.compared);
                // None of them has become old enough to delete.
                for (const std::size_t i : overlapping) {
                    assert(!AgedOut(tick, images[i].accessed));
                }
            }
        }
        // The run exercised hits and each kind of miss.
        assert(found > 1000);
        assert(missed_changed > 100);
        assert(missed_tick > 100);
        assert(missed_other > 100);
    }
    return 0;
}
