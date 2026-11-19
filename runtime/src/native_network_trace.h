#pragma once

#include <nx/module.h>
#include <array>
#include <atomic>
#include <span>
#include <string_view>

namespace nx::native_net {

constexpr size_t kMaxHooks = 15;
enum class ModeSource { None, StateX0, ControlX1 };
// Safe numeric-address copy supplied by the host. No direct guest dereference.
using ScalarReader = bool (*)(uint64_t address, uint32_t& value) noexcept;
struct GuardedSite {
    uint64_t offset = 0, prefix_hash = 0;
    const char* operation = ""; // Caller-owned static storage, like SDK labels.
    bool mode_from_x1 = false;
    ModeSource mode_source = ModeSource::None;
};
inline uint64_t prefix_hash(std::span<const uint8_t> bytes) {
    uint64_t hash=0xcbf29ce484222325ull;
    for(auto byte:bytes) hash=(hash^byte)*0x100000001b3ull;
    return hash;
}
struct Region { uint64_t base = 0, size = 0, text_offset = 0; };
struct Event {
    const char* operation = "";
    // Only allowlisted integer arguments; absent/out-of-range values are -1.
    // Mode: [bool]; Socket: [family,type,protocol]; Bind: [sockaddr length];
    // SendTo/RecvFrom: [byte capacity,flags,-1,SendTo sockaddr length];
    // Poll: [descriptor count,timeout ms]. Main object probes: [mode0..3,
    // copy_succeeded0/1], observed on entry, not successful activation.
    // Other APIs have no scalar fields.
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
    // Validate the complete site set before changing any entry. Prefix hashes
    // are identity guards, not authentication. Optional scalar reads use only
    // the supplied safe-copy callback, never direct guest object dereferences.
    size_t install_main(bool enabled, const ModuleInfo& main, Fn* table,
                        std::span<const uint8_t> image, std::string_view expected_build,
                        std::span<const GuardedSite> sites, Sink sink,
                        ScalarReader reader = nullptr);
    std::array<Counters, kMaxHooks> snapshot() const noexcept;
    size_t size() const noexcept { return count_; }
    void invoke(size_t index, CPU& cpu);
private:
    struct Slot {
        Fn* entry = nullptr;
        Fn original = nullptr;
        size_t kind = 0;
        const char* operation = "";
        ModeSource mode_source = ModeSource::None;
        std::atomic<uint64_t> calls{0}, errors{0}, exceptions{0}, total_ns{0}, peak_ns{0};
    };
    std::array<Slot, kMaxHooks> slots_{};
    size_t count_ = 0;
    Region main_{}, sdk_{};
    Sink sink_ = nullptr;
    ScalarReader scalar_reader_ = nullptr;
    std::atomic<uint64_t> emitted_{0};
};

// NX_NATIVE_NET_TRACE=1 only. SDK wrappers and guarded main observation, no HLE overrides.
void init();
// Emit at most one bounded cumulative summary. No table restoration or guest
// state changes; concurrent guest calls may still finish after the snapshot.
void shutdown() noexcept;
// Relaxed cumulative snapshot, empty when disabled. No exchange/render lock;
// fields may reflect slightly different instants during concurrent SDK calls.
std::array<Counters, kMaxHooks> diagnostics() noexcept;
} // namespace nx::native_net
