// DecodeReuseEntry (video_core/texture_cache/lookup_reuse.h): the value decoded for a key is
// returned for later uses of that key, and is decoded again only when the key changes.
#include <cassert>
#include <cstdio>
#include <string>

#include "video_core/texture_cache/lookup_reuse.h"

int main() {
    using Entry = VideoCore::DecodeReuseEntry<int, std::string>;
    Entry entry;
    int decodes = 0;
    const auto decode = [&decodes](int key) {
        return [&decodes, key] {
            ++decodes;
            return std::to_string(key * 7);
        };
    };

    // The first use decodes.
    assert(entry.Get(3, decode(3)) == "21");
    assert(decodes == 1);
    // The same key takes the stored value.
    assert(entry.Get(3, decode(3)) == "21");
    assert(entry.Get(3, decode(3)) == "21");
    assert(decodes == 1);
    // A new key decodes once, then reuses.
    assert(entry.Get(4, decode(4)) == "28");
    assert(decodes == 2);
    assert(entry.Get(4, decode(4)) == "28");
    assert(decodes == 2);
    // Only the last key is kept, so the first key decodes again.
    assert(entry.Get(3, decode(3)) == "21");
    assert(decodes == 3);

    // A returned copy belongs to the caller: changing it leaves the stored value alone.
    std::string copy = entry.Get(3, decode(3));
    copy += "x";
    assert(entry.Get(3, decode(3)) == "21");
    assert(decodes == 3);

    // Whatever the key sequence, the result is what a fresh decode of that key gives.
    for (int i = 0; i < 1000; ++i) {
        const int key = (i / 3) % 5;
        const std::string got = entry.Get(key, decode(key));
        assert(got == std::to_string(key * 7));
    }

    std::printf("test_decode_reuse: ok (%d decodes)\n", decodes);
    return 0;
}
