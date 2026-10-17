// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>
#include "video_core/buffer_cache/download_rescan.h"
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#c); std::abort(); } } while(false)
int main() {
    using namespace VideoCore;
    std::atomic<std::uint64_t> changes{0};
    DownloadRescan scan;
    int calls=0;
    bool pending=true;
    auto predicate=[&]{++calls;return pending;};
    CHECK(scan.Pending(changes,true,predicate));
    for(int i=0;i<1000;++i) CHECK(scan.Pending(changes,true,predicate));
    CHECK(calls==1);
    changes.fetch_add(1,std::memory_order_release); // unrelated completion, overlap remains
    CHECK(scan.Pending(changes,true,predicate)); CHECK(calls==2);
    changes.fetch_add(1,std::memory_order_release); // newer overlapping copy added
    CHECK(scan.Pending(changes,true,predicate)); CHECK(calls==3);
    pending=false; changes.fetch_add(1,std::memory_order_release);
    CHECK(!scan.Pending(changes,true,predicate)); CHECK(calls==4);
    DownloadRescan completed; CHECK(!completed.Pending(changes,true,predicate));
    completed.Pending(changes,false,predicate); completed.Pending(changes,false,predicate);
    CHECK(calls==7); // disabled always evaluates original predicate
    // Completion in the locked predicate between epoch load and next poll is not lost.
    DownloadRescan race; pending=true;
    CHECK(race.Pending(changes,true,[&]{pending=false;changes.fetch_add(1,std::memory_order_release);return true;}));
    CHECK(!race.Pending(changes,true,predicate));
    // Callback publishes backing data before removing ownership; wait sees every overlapping copy.
    for(int trial=0;trial<200;++trial) {
        std::mutex mutex;
        std::vector<int> downloads{1,2};
        int backing=0;
        std::atomic<std::uint64_t> epoch{};
        std::thread writer([&]{
            for(int id:{1,2}) {
                std::scoped_lock lock{mutex};
                backing+=id; downloads.pop_back();
                epoch.fetch_add(1,std::memory_order_release);
            }
        });
        DownloadRescan concurrent;
        while(concurrent.Pending(epoch,true,[&]{std::scoped_lock lock{mutex};return !downloads.empty();})) std::this_thread::yield();
        CHECK(backing==3); writer.join();
    }
}
