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

// Keep the scheduled phase instead of adding each completed present's delay to
// the next period. A whole missed period, first frame or clock reversal resets
// to the actual completion time, avoiding a long catch-up burst after a stall.
constexpr uint64_t framegen_present_anchor(uint64_t target, uint64_t presented, uint64_t period) {
    if (target && period && presented >= target && presented - target < period) return target;
    return presented;
}

}
