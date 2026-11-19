#pragma once
#include <cstdint>
#include <cstring>

namespace nx::nvn {
inline bool update_texture_content_hash(uint64_t observed,uint64_t& previous,bool& valid) {
    const bool changed=valid && observed!=previous;
    previous=observed;valid=true;return changed;
}
// Four independent lanes retain the fast whole-word path. Unaligned pool
// addresses are legal, and every trailing byte must participate in dirty checks.
inline uint64_t texture_content_hash(const uint8_t* p, uint64_t size) {
    uint64_t h[4] = {0x9E3779B97F4A7C15ull, 1, 2, 3}, n = size / 32;
    for (uint64_t k = 0; k < n; ++k)
        for (int lane = 0; lane < 4; ++lane) {
            uint64_t word; std::memcpy(&word, p + 32 * k + 8 * lane, 8);
            h[lane] = (h[lane] ^ word) * 0x100000001B3ull + (h[lane] >> 29);
        }
    uint64_t r = h[0] ^ h[1] * 3 ^ h[2] * 5 ^ h[3] * 7;
    uint64_t at = n * 32;
    for (; size - at >= 8; at += 8) {
        uint64_t word; std::memcpy(&word, p + at, 8);
        r = (r ^ word) * 0x100000001B3ull + (r >> 29);
    }
    for (; at < size; ++at) r = (r ^ p[at]) * 0x100000001B3ull + (r >> 29);
    return r;
}
}
