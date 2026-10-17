// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdio>
#include <cstdlib>
#include "video_core/texture_cache/lookup_reuse.h"
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#c); std::abort(); } } while(false)
struct Result { int id, mip, layer; };
int main() {
    using namespace VideoCore;
    LookupReuseEntry<TextureLookupKey, Result> cache;
    TextureLookupKey key{};
    key.program = 7;
    key.specialization = &cache;
    CHECK(!cache.Find(key, 1));
    cache.Store(key, 1, {42, 3, 2});
    auto hit = cache.Find(key, 1);
    CHECK(hit && hit->id == 42 && hit->mip == 3 && hit->layer == 2);
    // Every descriptor word participates, including mip, layer, address and view format bits.
    for (int i=0; i<8; ++i) { auto other=key; other.descriptor[i]^=1; CHECK(!cache.Find(other,1)); }
    auto other=key; other.specialization=nullptr; CHECK(!cache.Find(other,1));
    other=key; other.program++; CHECK(!cache.Find(other,1));
    for (unsigned bit=0;bit<5;++bit) {other=key;other.resource_flags^=1u<<bit;CHECK(!cache.Find(other,1));}
    other=key; other.mip_mode++; CHECK(!cache.Find(other,1));
    other=key; other.mip_index++; CHECK(!cache.Find(other,1));
    other=key; other.binding_index++; CHECK(!cache.Find(other,1));
    other=key; other.alt_tile=true; CHECK(!cache.Find(other,1));
    CHECK(!cache.Find(key,2)); // deletion/replacement, even if allocator reuses image id
    cache.Store(key,2,{42,0,0});
    CHECK(cache.Find(key,2)->mip==0);
    cache.Invalidate(); CHECK(!cache.Find(key,2)); // needs_rebind
    cache.Store(other,3,{17,6,4});
    CHECK(!cache.Find(key,3)); CHECK(cache.Find(other,3)->layer==4);
}
