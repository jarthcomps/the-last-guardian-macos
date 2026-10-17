// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "video_core/coherent_read.h"
#include "video_core/buffer_cache/download_overlap.h"
#include "video_core/buffer_cache/download_rescan.h"
#include "video_core/buffer_cache/readback_window.h"
#include "video_core/texture_cache/lookup_reuse.h"
#define CHECK(c) do { if (!(c)) {std::fprintf(stderr,"line %d: %s\n",__LINE__,#c);std::abort();} } while(false)
int main() {
    using namespace VideoCore;
    std::array<unsigned char,32> guest{}, backing{};
    const auto address=reinterpret_cast<std::uintptr_t>(guest.data());
    PendingBackingCopies copies;
    bool gpu_dirty=false;
    auto current=[&](auto addr,auto size){return !gpu_dirty && !copies.Overlaps(addr,size);};
    auto shared=[&](auto addr,auto size)->const void* {
        CHECK(addr==address && size==guest.size());return backing.data();
    };
    CHECK(CoherentReadPointer(address,guest.size(),true,current,shared)==backing.data());
    const std::array pending{std::pair{address+8,address+16}};
    copies.Add(1,pending); // a partial copy blocks hashing the entire image
    CHECK(CoherentReadPointer(address,guest.size(),true,current,shared)==guest.data());
    LookupReuseEntry<TextureLookupKey,int> lookup;
    TextureLookupKey key{};lookup.Store(key,10,42);
    CHECK(*lookup.Find(key,10)==42); // cache hit must not replace the memory coherency check
    CHECK(CoherentReadPointer(address,guest.size(),true,current,shared)==guest.data());
    std::atomic<std::uint64_t> epoch{};
    DownloadRescan rescan;
    auto pending_copy=[&]{return copies.Overlaps(address,guest.size());};
    CHECK(rescan.Pending(epoch,true,pending_copy));
    copies.Add(2,pending); epoch.fetch_add(1,std::memory_order_release);
    backing[8]=7;copies.Remove(1);epoch.fetch_add(1,std::memory_order_release);
    CHECK(rescan.Pending(epoch,true,pending_copy)); // newer overlapping copy still owns bytes
    CHECK(CoherentReadPointer(address,guest.size(),true,current,shared)==guest.data());
    guest=backing; copies.Remove(2);epoch.fetch_add(1,std::memory_order_release);
    CHECK(!rescan.Pending(epoch,true,pending_copy));
    CHECK(CoherentReadPointer(address,guest.size(),true,current,shared)==backing.data());
    CHECK(std::memcmp(guest.data(),backing.data(),guest.size())==0);
    gpu_dirty=true;CHECK(CoherentReadPointer(address,guest.size(),true,current,shared)==guest.data());
    CHECK(!lookup.Find(key,11)); // replacement invalidates image id even if same address
    // A smaller transfer window still covers every byte of a cross-boundary request.
    for(auto window:{64u*1024,512u*1024}) {
        const auto [begin,end]=CalculateReadbackWindow(0x100000,0x400000,0x10fff8,32,window);
        CHECK(begin<=0x10fff8 && end>=0x110018);
    }
}
