#include "netplay_timing.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
void check(bool ok, const char* text) { if (!ok) { std::fprintf(stderr, "%s\n", text); std::exit(1); } }
int main() {
    nx::lan::LockstepTiming timing;
    check(timing.snapshot().ticks == 0, "no invented samples before joining");
    timing.record(10'000'000);
    timing.record(50'000'000);
    timing.record(51'000'000);
    auto values = timing.snapshot();
    check(values.ticks == 3 && values.waits_over_50_ms == 1 && values.last_ms == 51 && values.peak_ms == 51 &&
          std::abs(values.smoothed_ms - 19.5) < 1e-9, "measured waits, peak, threshold and smoothing remain distinct");
    timing.reset();
    check(timing.snapshot().ticks == 0 && timing.snapshot().peak_ms == 0, "new sessions do not inherit old peaks");
    timing.record(std::numeric_limits<uint64_t>::max());
    timing.record(std::numeric_limits<uint64_t>::max());
    check(timing.snapshot().smoothed_ms > 1e12, "large diagnostic values do not overflow the smoother");
    std::puts("LAN barrier timing: startup, slow waits, smoothing, reset and overflow passed");
}
