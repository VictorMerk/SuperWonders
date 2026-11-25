#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace nx::nvn {

// GPU results can be mapped through uncached upload memory. Read wider blocks
// once, then compare their saved values; only GPU-changed words may replace
// guest memory, whose other words can have changed since the submit.
// The source mapping, snapshot and guest destination are distinct allocations.
inline void copy_changed_words(uint8_t* to, const uint8_t* from, const uint8_t* before, uint64_t size) {
    uint64_t k = 0;
    for (; k + 32 <= size; k += 32) {
        alignas(32) uint8_t observed[32];
        std::memcpy(observed,from+k,sizeof observed);
        if (std::memcmp(observed,before+k,sizeof observed) == 0) continue;
        for (uint64_t b=0;b<sizeof observed;b+=4)
            if (std::memcmp(observed+b,before+k+b,4) != 0) std::memcpy(to+k+b,observed+b,4);
    }
    for (; k + 8 <= size; k += 8) {
        if (std::memcmp(from+k,before+k,8) == 0) continue;
        for (uint64_t b=k;b<k+8;b+=4)
            if (std::memcmp(from+b,before+b,4) != 0) std::memcpy(to+b,from+b,4);
    }
    for (; k<size;++k) if (from[k] != before[k]) to[k] = from[k];
}

}  // namespace nx::nvn
