#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <ostream>
#include <span>
#include <string_view>
#include <vector>

namespace nx::nvn::pipeline_trace {
enum class Operation { Miss, CacheProbe, AsyncQueued, AsyncWait, AsyncObserved, SyncCompile, GplLibrariesAndLink, DrawSkipped };
struct Identity {
    uint64_t key = 0, frame = 0;
    char vs[17]{}, fs[17]{};
    bool caller_may_skip = false, effective_may_skip = false, gpl = false;
};
struct Event {
    Identity identity;
    Operation operation = Operation::Miss;
    uint64_t sequence = 0, observed_ns = 0, duration_ns = 0;
    int32_t result = 0;
    bool result_known = false;
};
// Fixed retention after construction; overwrite oldest events, count all events.
// Durations can overlap/nest and worker durations are observed later, not RTT or
// exclusive replay costs. frame is a non-atomic association with the pipeline key.
class Recorder {
    std::mutex mutex_;
    std::vector<Event> ring_;
    size_t next_ = 0, size_ = 0;
    uint64_t total_ = 0;
    bool frozen_ = false;
public:
    explicit Recorder(size_t capacity);
    void capture(Event) noexcept;
    void freeze_and_write(std::ostream&); // permanent freeze; no per-event I/O
};
uint64_t fingerprint(std::span<const std::byte>) noexcept;
Identity identity(uint64_t key, uint64_t frame, std::string_view vs, std::string_view fs,
                  bool caller_may_skip, bool effective_may_skip, bool gpl) noexcept;
bool enabled() noexcept; // NX_PIPELINE_MISS_TRACE=<path>, cached once; default off
uint64_t now_ns() noexcept;
void capture(const Identity&, Operation, uint64_t duration_ns = 0,
             int32_t result = 0, bool result_known = false) noexcept;
void shutdown() noexcept; // parent platform shutdown must call before ExitProcess
}
