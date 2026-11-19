#pragma once

#include <nx/module.h>
#include <array>
#include <atomic>

namespace nx::native_net {

constexpr size_t kMaxHooks = 10;
struct Region { uint64_t base = 0, size = 0, text_offset = 0; };
struct Event {
    const char* operation = "";
    // Only allowlisted integer arguments; absent/out-of-range values are -1.
    // Mode: [bool]; Socket: [family,type,protocol]; Bind: [sockaddr length];
    // SendTo/RecvFrom: [byte capacity,flags,-1,SendTo sockaddr length];
    // Poll: [descriptor count,timeout ms]. Other APIs have no scalar fields.
    std::array<int64_t, 4> fields{-1, -1, -1, -1};
    uint64_t call = 0, caller_offset = 0, duration_ns = 0;
    const char* caller_module = "other";
    int64_t result = 0;
    bool has_result = false, error = false, exception = false;
};
struct Counters {
    const char* operation = "";
    uint64_t calls = 0, errors = 0, exceptions = 0, total_ns = 0, peak_ns = 0;
};
using Sink = void (*)(const Event&);

// Startup/shutdown only: function tables must not be executing during install or
// destruction. One census can own wrappers at a time. Calls and snapshots can
// run concurrently; snapshots never acquire the networking/game exchange lock.
class Census {
public:
    Census() = default;
    ~Census();
    Census(const Census&) = delete;
    Census& operator=(const Census&) = delete;
    size_t install(bool enabled, const ModuleInfo& sdk, Fn* table, Region main,
                   Region sdk_text, Sink sink);
    std::array<Counters, kMaxHooks> snapshot() const noexcept;
    size_t size() const noexcept { return count_; }
    void invoke(size_t index, CPU& cpu);
private:
    struct Slot {
        Fn* entry = nullptr;
        Fn original = nullptr;
        size_t kind = 0;
        std::atomic<uint64_t> calls{0}, errors{0}, exceptions{0}, total_ns{0}, peak_ns{0};
    };
    std::array<Slot, kMaxHooks> slots_{};
    size_t count_ = 0;
    Region main_{}, sdk_{};
    Sink sink_ = nullptr;
    std::atomic<uint64_t> emitted_{0};
};

// NX_NATIVE_NET_TRACE=1 only. Installs SDK wrappers, never HLE overrides.
void init();
// Relaxed cumulative snapshot, empty when disabled. No exchange/render lock;
// fields may reflect slightly different instants during concurrent SDK calls.
std::array<Counters, kMaxHooks> diagnostics() noexcept;
} // namespace nx::native_net
