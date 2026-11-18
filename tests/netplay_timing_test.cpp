#include "netplay_timing.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>
void check(bool ok, const char* text) { if (!ok) { std::fprintf(stderr, "%s\n", text); std::exit(1); } }
int main() {
    for (uint32_t rate:{30u,60u}) {
        nx::lan::FramePacer pacer;
        const uint64_t period=1'000'000'000ull/rate, start=1'000'000'000;
        check(pacer.wait_ns(start,rate)==0,"first LAN present does not add a startup wait");
        check(pacer.wait_ns(start+5'000'000,rate)==period-5'000'000,"local work and transport consume the same frame budget");
        check(pacer.wait_ns(start+period+7'000'000,rate)==period-7'000'000,"normal deadlines retain phase without monitor tick rounding");
        const auto stalled=start+period*10;
        check(pacer.wait_ns(stalled,rate)==0,"late frame adds no wait or catch-up burst");
        check(pacer.wait_ns(stalled+1'000'000,rate)==period-1'000'000,"a stall rebases the next frame budget");
        pacer.reset();
        check(pacer.wait_ns(stalled+2'000'000,rate)==0,"new session starts with no inherited deadline");
    }
    nx::lan::LockstepTiming timing;
    nx::lan::FrameWorkClock work;
    check(work.elapsed(100'000'000)==0,"first peer frame duration is unknown, not invented");
    work.finish(140'000'000);  // preceding frame ended at 100 ms, then waited 40 ms in LAN
    check(work.elapsed(160'000'000)==20'000'000,"peer work excludes the preceding 40 ms LAN barrier");
    check(work.elapsed(139'000'000)==0,"backward timestamps never underflow into huge work times");
    work.reset(); check(work.elapsed(200'000'000)==0,"new sessions reset the peer work clock");
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
