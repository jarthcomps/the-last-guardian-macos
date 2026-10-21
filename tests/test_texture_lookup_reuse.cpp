// Texture lookup reuse: the key holds every input of a lookup, and an entry answers only for the
// same key at the same image generation (video_core/texture_cache/lookup_reuse.h).
#include <cassert>
#include <cstdint>

#include "video_core/texture_cache/lookup_reuse.h"

using VideoCore::LookupReuseEntry;
using VideoCore::TextureLookupKey;

namespace {

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
        // A result is reused for the same key at the same image generation and GC tick, and
        // nothing else.
        LookupReuseEntry<TextureLookupKey, int> entry;
        const TextureLookupKey key = BaseKey();
        assert(entry.Find(key, 0, 0) == nullptr);

        entry.Store(key, 5, 9, 42);
        const int* hit = entry.Find(key, 5, 9);
        assert(hit && *hit == 42);
        // An image was registered or removed since: the result is not used.
        assert(entry.Find(key, 6, 9) == nullptr);
        // A GC tick passed since: an overlapping image may be old enough to be freed now.
        assert(entry.Find(key, 5, 10) == nullptr);

        TextureLookupKey other = key;
        other.binding_index = 4;
        assert(entry.Find(other, 5, 9) == nullptr);

        // One entry per slot: the new result replaces the old one.
        entry.Store(other, 6, 10, 43);
        assert(entry.Find(key, 5, 9) == nullptr);
        const int* replaced = entry.Find(other, 6, 10);
        assert(replaced && *replaced == 43);

        entry.Invalidate();
        assert(entry.Find(other, 6, 10) == nullptr);
    }
    return 0;
}
