#pragma once
#include <atomic>
#include <cstdint>

namespace nx::lan {
// LAN peers have independent display clocks. Pace against elapsed host time,
// including transport waits, without rounding every late frame to a display
// tick or running catch-up bursts after a stall. One simulation-thread owner.
class FramePacer {
    uint64_t next=0;
public:
    uint64_t wait_ns(uint64_t now, uint32_t rate) {
        const uint64_t period=1'000'000'000ull/(rate==30?30:60);
        if (!next || now>=next) { next=now+period; return 0; }
        const uint64_t wait=next-now;
        next+=period;
        return wait;
    }
    void reset() { next=0; }
};
struct TimingSnapshot {
    uint64_t ticks = 0, waits_over_50_ms = 0;
    double last_ms = 0, smoothed_ms = 0, peak_ms = 0;
};
struct PeerFrameTiming {
    uint64_t samples=0;
    double last_ms=0,smoothed_ms=0,peak_ms=0;
};
// One simulation-thread producer. Finish after the LAN barrier, then measure
// until the next framework frame ends: peer work must not include that wait.
class FrameWorkClock {
    uint64_t last_end=0;
public:
    void reset() { last_end=0; }
    void finish(uint64_t now) { last_end=now; }
    uint64_t elapsed(uint64_t now) const { return last_end && now>=last_end?now-last_end:0; }
};
// One producer: the gameplay input exchange. Readers may query the HUD from
// another thread. This times the whole lockstep barrier, including the other
// machine's simulation/render delays; it is deliberately not called ping/RTT.
class LockstepTiming {
    std::atomic<uint64_t> ticks{0}, slow{0}, last{0}, smoothed{0}, peak{0};
public:
    void reset() { ticks = 0; slow = 0; last = 0; smoothed = 0; peak = 0; }
    void record(uint64_t ns) {
        const auto count = ticks.load(std::memory_order_relaxed);
        const auto previous = smoothed.load(std::memory_order_relaxed);
        // Division first avoids overflowing when testing extreme timestamps.
        const auto average = count ? previous / 8 * 7 + ns / 8 + (previous % 8 * 7 + ns % 8) / 8 : ns;
        smoothed.store(average, std::memory_order_relaxed);
        last.store(ns, std::memory_order_relaxed);
        if (ns > peak.load(std::memory_order_relaxed)) peak.store(ns, std::memory_order_relaxed);
        if (ns > 50'000'000) slow.fetch_add(1, std::memory_order_relaxed);
        ticks.fetch_add(1, std::memory_order_relaxed);
    }
    TimingSnapshot snapshot() const {
        return {ticks.load(std::memory_order_relaxed), slow.load(std::memory_order_relaxed),
            double(last.load(std::memory_order_relaxed)) / 1e6,
            double(smoothed.load(std::memory_order_relaxed)) / 1e6,
            double(peak.load(std::memory_order_relaxed)) / 1e6};
    }
};
}
