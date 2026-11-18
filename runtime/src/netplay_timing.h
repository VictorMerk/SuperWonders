#pragma once
#include <atomic>
#include <cstdint>

namespace nx::lan {
struct TimingSnapshot {
    uint64_t ticks = 0, waits_over_50_ms = 0;
    double last_ms = 0, smoothed_ms = 0, peak_ms = 0;
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
