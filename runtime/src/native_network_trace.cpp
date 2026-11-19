#include "native_network_trace.h"
#include <chrono>
#include <cstring>
#include <iterator>
#include <utility>

namespace nx::native_net {
namespace {
enum Kind { Mode, IpConfig, Socket, Bind, SendTo, RecvFrom, Poll, Errno, Ssl, Friends };
struct Spec { const char* symbol; const char* label; };
constexpr Spec specs[] = {
    {"_ZN2nn4nifm19SetLocalNetworkModeEb", "nifm.SetLocalNetworkMode"},
    {"_ZN2nn4nifm22GetCurrentIpConfigInfoEP7in_addrS2_S2_S2_S2_", "nifm.GetCurrentIpConfigInfo"},
    {"_ZN2nn6socket6SocketEiii", "socket.Socket"},
    {"_ZN2nn6socket4BindEiPK8sockaddrj", "socket.Bind"},
    {"_ZN2nn6socket6SendToEiPKvmiPK8sockaddrj", "socket.SendTo"},
    {"_ZN2nn6socket8RecvFromEiPvmiP8sockaddrPj", "socket.RecvFrom"},
    {"_ZN2nn6socket4PollEP6pollfdmi", "socket.Poll"},
    {"_ZN2nn6socket12GetLastErrnoEv", "socket.GetLastErrno"},
    {"_ZN2nn3ssl10InitializeEv", "ssl.Initialize"},
    {"_ZN2nn7friends27CheckFriendListAvailabilityEPbRKNS_7account3UidE", "friends.CheckFriendListAvailability"},
};
static_assert(std::size(specs) == kMaxHooks);
Census* owner = nullptr; // Published before guest execution, retired after it stops.
template<size_t I> void wrapper(CPU& cpu) { owner->invoke(I, cpu); }
template<size_t... I> constexpr auto wrappers(std::index_sequence<I...>) {
    return std::array<Fn, sizeof...(I)>{wrapper<I>...};
}
constexpr auto thunks = wrappers(std::make_index_sequence<kMaxHooks>{});
uint64_t now() noexcept {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
int64_t bounded(uint64_t value, uint64_t max) { return value <= max ? int64_t(value) : -1; }
void peak(std::atomic<uint64_t>& target, uint64_t value) {
    auto old = target.load(std::memory_order_relaxed);
    while (old < value && !target.compare_exchange_weak(old, value, std::memory_order_relaxed)) {}
}
bool within(uint64_t addr, Region r) {
    return addr >= r.base && addr - r.base >= r.text_offset &&
        addr - r.base - r.text_offset < r.size;
}
}

size_t Census::install(bool enabled, const ModuleInfo& sdk, Fn* table, Region main,
                       Region sdk_text, Sink sink) {
    if (!enabled || !table || count_ || owner || !sdk.symbols || sdk.text_size < 4) return 0;
    main_ = main; sdk_ = sdk_text; sink_ = sink;
    // No symbol names or pointers from guest memory are ever sent to the sink.
    for (size_t kind = 0; kind < std::size(specs); ++kind) {
        for (size_t i = 0; i < sdk.symbol_count; ++i) {
            const auto& symbol = sdk.symbols[i];
            if (!symbol.name || symbol.imported || symbol.type != 2 ||
                std::strcmp(symbol.name, specs[kind].symbol) || (symbol.value & 3) ||
                symbol.value < sdk.text || symbol.value - sdk.text > sdk.text_size - 4 ||
                // Current loader allocates text_size/4 entries indexed by module
                // offset, not text-relative offset. Respect that allocation too.
                symbol.value > sdk.text_size - 4) continue;
            Fn* entry = table + symbol.value / 4;
            if (!*entry) continue;
            bool duplicate = false;
            for (size_t j = 0; j < count_; ++j) if (slots_[j].entry == entry) duplicate = true;
            if (duplicate) continue;
            auto& slot = slots_[count_];
            slot.entry = entry; slot.original = *entry; slot.kind = kind;
            owner = this;
            *entry = thunks[count_++];
            break;
        }
    }
    return count_;
}
Census::~Census() {
    for (size_t i = 0; i < count_; ++i)
        if (*slots_[i].entry == thunks[i]) *slots_[i].entry = slots_[i].original;
    if (owner == this) owner = nullptr;
}
std::array<Counters, kMaxHooks> Census::snapshot() const noexcept {
    std::array<Counters, kMaxHooks> result{};
    for (size_t i = 0; i < count_; ++i) {
        const auto& s = slots_[i];
        result[i] = {specs[s.kind].label, s.calls.load(std::memory_order_relaxed),
            s.errors.load(std::memory_order_relaxed), s.exceptions.load(std::memory_order_relaxed),
            s.total_ns.load(std::memory_order_relaxed), s.peak_ns.load(std::memory_order_relaxed)};
    }
    return result;
}
void Census::invoke(size_t index, CPU& cpu) {
    auto& slot = slots_[index];
    Event event;
    event.operation = specs[slot.kind].label;
    event.call = slot.calls.fetch_add(1, std::memory_order_relaxed) + 1;
    const auto lr = cpu.x[30];
    if (within(lr, main_)) { event.caller_module = "main"; event.caller_offset = lr - main_.base; }
    else if (within(lr, sdk_)) { event.caller_module = "sdk"; event.caller_offset = lr - sdk_.base; }
    auto& f = event.fields;
    switch (slot.kind) {
    case Mode: f[0] = bounded(cpu.x[0], 1); break;
    case Socket: // family, type/flags, protocol; no descriptor or address.
        f = {bounded(cpu.x[0], 255), bounded(cpu.x[1], 0xffffff), bounded(cpu.x[2], 65535), -1}; break;
    case Bind: f[0] = bounded(cpu.x[2], 256); break; // sockaddr length only
    case SendTo: f[3] = bounded(cpu.x[5], 256); [[fallthrough]];
    case RecvFrom:
        f[0] = bounded(cpu.x[2], 64 * 1024 * 1024); f[1] = bounded(cpu.x[3], 0xffff); break;
    case Poll:
        f[0] = bounded(cpu.x[1], 65536);
        if (int32_t(cpu.x[2]) >= -1 && int32_t(cpu.x[2]) <= 3600000) f[1] = int32_t(cpu.x[2]);
        break;
    default: break; // pointer-only APIs: never inspect/dereference any argument.
    }
    const auto start = now();
    auto finish = [&] {
        event.duration_ns = now() - start;
        slot.total_ns.fetch_add(event.duration_ns, std::memory_order_relaxed);
        peak(slot.peak_ns, event.duration_ns);
        if (event.error) slot.errors.fetch_add(1, std::memory_order_relaxed);
        if (event.exception) slot.exceptions.fetch_add(1, std::memory_order_relaxed);
        // First four calls then powers of two, at most 256 events in this process.
        if (sink_ && (event.call <= 4 || !(event.call & (event.call - 1))) &&
            emitted_.fetch_add(1, std::memory_order_relaxed) < 256) {
            try { sink_(event); } catch (...) {} // diagnostics cannot replace a guest exception.
        }
    };
    try { slot.original(cpu); }
    catch (...) { event.exception = true; finish(); throw; }
    if (slot.kind != Mode) {
        event.has_result = true;
        if (slot.kind == IpConfig || slot.kind == Ssl || slot.kind == Friends) {
            event.result = uint32_t(cpu.x[0]); event.error = event.result != 0;
        } else {
            const auto value = slot.kind == SendTo || slot.kind == RecvFrom ? int64_t(cpu.x[0]) : int64_t(int32_t(cpu.x[0]));
            // Error/success scalar only; reject unexpected pointer-sized returns.
            if (value >= -65536 && value <= 64 * 1024 * 1024) event.result = value;
            else event.has_result = false;
            event.error = slot.kind != Errno && event.has_result && value < 0;
        }
    }
    finish();
}
} // namespace nx::native_net
