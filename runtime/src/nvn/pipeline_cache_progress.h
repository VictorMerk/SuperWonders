#pragma once
#include <atomic>
#include <cstdint>

namespace nx::nvn {

// A same-size driver snapshot may still contain new data. Remember that it was
// inspected without declaring it persisted: skip repeat periodic queries until
// another mutation, but keep the forced shutdown save owed.
class PipelineCacheProgress {
public:
    void changed() { revision_.fetch_add(1, std::memory_order_release); }
    uint64_t revision() const { return revision_.load(std::memory_order_acquire); }
    bool dirty() const { return revision() != saved_.load(std::memory_order_acquire); }
    bool periodic_due() const {
        return dirty() && revision() != attempted_.load(std::memory_order_acquire);
    }
    void not_grown(uint64_t revision) { attempted_.store(revision, std::memory_order_release); }
    void saved(uint64_t revision) {
        saved_.store(revision, std::memory_order_release);
        attempted_.store(revision, std::memory_order_release);
    }
    void failed() {
        // Retry even if a forced save followed an earlier same-size observation.
        attempted_.store(saved_.load(std::memory_order_acquire), std::memory_order_release);
    }
private:
    std::atomic<uint64_t> revision_{0}, saved_{0}, attempted_{0};
};

}  // namespace nx::nvn
