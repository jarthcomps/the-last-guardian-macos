// Checks DebugStateType::WaitingDumpFlag (src/core/waiting_dump_flag.h) against the map it mirrors.
// DebugStateImpl::PushQueueDump emplaces into waiting_reg_dumps and calls Set(true); GetRegDump
// erases and calls Set(!empty). After every such step the flag must equal "the map is not empty".

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "core/waiting_dump_flag.h"

using DebugStateType::WaitingDumpFlag;

namespace {

// Same operations as the two writers in debug_state.cpp, on a map of the same key type.
void Push(WaitingDumpFlag& flag, std::unordered_map<std::size_t, int>& waiting, std::size_t addr) {
    waiting.emplace(addr, 0);
    flag.Set(true); // an emplace always leaves the map non-empty
}

void Take(WaitingDumpFlag& flag, std::unordered_map<std::size_t, int>& waiting, std::size_t addr) {
    const auto it = waiting.find(addr);
    if (it == waiting.end()) {
        return; // GetRegDump returns before touching anything
    }
    waiting.erase(it);
    flag.Set(!waiting.empty());
}

} // namespace

int main() {
    {
        // Starts empty, and stays that way until a push.
        WaitingDumpFlag flag;
        std::unordered_map<std::size_t, int> waiting;
        assert(!flag.Waiting());
        Take(flag, waiting, 0x1000);
        assert(!flag.Waiting());
        Push(flag, waiting, 0x1000);
        assert(flag.Waiting());
        // A repeated key changes nothing and keeps the flag set.
        Push(flag, waiting, 0x1000);
        assert(flag.Waiting() && waiting.size() == 1);
        // A missing key changes nothing.
        Take(flag, waiting, 0x2000);
        assert(flag.Waiting() && waiting.size() == 1);
        Push(flag, waiting, 0x2000);
        Take(flag, waiting, 0x1000);
        assert(flag.Waiting() && waiting.size() == 1);
        Take(flag, waiting, 0x2000);
        assert(!flag.Waiting() && waiting.empty());
    }
    {
        // Random walk over a small key range: after every operation the flag equals !empty().
        WaitingDumpFlag flag;
        std::unordered_map<std::size_t, int> waiting;
        std::uint64_t state = 0x9E3779B97F4A7C15ULL;
        for (int step = 0; step < 20000; ++step) {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            const auto key = static_cast<std::size_t>((state >> 33) % 16) * 0x40;
            if (((state >> 20) & 3) != 0) {
                Push(flag, waiting, key);
            } else {
                Take(flag, waiting, key);
            }
            assert(flag.Waiting() == !waiting.empty());
        }
    }
    return 0;
}
