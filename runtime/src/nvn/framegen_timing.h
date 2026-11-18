#pragma once
#include <algorithm>
#include <cstdint>
namespace nx::nvn {
// Use the requested game cadence. Measuring a cadence that already contains the
// interpolation wait and feeding it back into that wait progressively slows play.
constexpr uint64_t framegen_present_period(int cap, int interval) {
    cap = std::max(1,cap);
    if (cap > 60) return 1'000'000'000ull/uint64_t(cap);
    const int ticks = std::max(std::max(1,interval),(60+cap-1)/cap);
    return 1'000'000'000ull*uint64_t(ticks)/60;
}
}
