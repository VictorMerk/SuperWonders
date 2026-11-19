#pragma once
#include "native_lan_nifm.h"
#include "native_lan_udp.h"
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

namespace nx::native_lan {
struct BackendOptions {
    bool enabled=false;
    std::optional<uint32_t> address;
    bool allow_loopback_fixture=false;
};
enum class BackendAccess { Network, Cleanup };
struct BackendTransport {
    std::shared_ptr<Udp> transport;
    int error=0;
    explicit operator bool() const { return transport && !error; }
};
// Complete OWNED local-provider lifecycle, not an original SDK client object.
// No SDK globals/init bits, accounts, online mode, hooks or environment activation.
// Caller owns coherent dispatch of ALL relevant SDK aliases before activating.
// Destroy only after joining users; finalize itself is safe with in-flight calls.
class Backend {
public:
    // Factory must return a fresh exclusively owned capability candidate; it is
    // invoked lazily outside all provider locks, so injected failures are testable.
    using TransportFactory=std::function<std::shared_ptr<Udp>()>;
    explicit Backend(BackendOptions options={},Nifm::Enumerator enumerate=enumerate_host_interfaces,
        TransportFactory factory={});
    ~Backend();
    Backend(const Backend&)=delete;
    Backend& operator=(const Backend&)=delete;
    // Real host capability probe: create/close UDP socket, no packet/enumeration.
    // Independent of network availability. Repeated ready initialize is a no-op.
    Result initialize_sockets();
    void finalize_sockets() noexcept;
    // Irreversible process/provider shutdown, distinct from reusable Finalize.
    // Cancels transport/request; blocked initialization cannot publish afterward.
    void stop() noexcept;
    bool socket_initialized() const;
    // Nifm can become available before SocketInitialize; no circular dependency.
    // Cached/no allocation, independently gated by explicit local request state.
    bool network_available() const noexcept;
    // Acquire for ONE bounded call, never cache across mode/lifecycle changes.
    // Network requires initialized socket provider AND requested available local
    // mode. Cleanup permits close/descriptor queries offline. Both reject after
    // finalize; prior refs own their old sockets until the operation finishes.
    // Mode/request changes gate future calls; admitted <=1s operations may finish.
    // Finalize cancels old Udp, so an in-flight Poll normally returns Interrupted;
    // an already-running nonblocking syscall may complete, as the core documents.
    BackendTransport acquire(BackendAccess access=BackendAccess::Network) const;
    // GetLastErrno/SetLastErrno dispatch must use socket_initialized: before it,
    // getter22 / setter-no-op are handled outside the direct TLS guest adapter.
    NifmStatus initialize_nifm();
    NifmStatus set_local_mode(bool local);
    NifmStatus submit_request();
    NifmStatus cancel_request();
    NifmStatus refresh();
    NifmSnapshot snapshot() const;
    NifmConfigResult config(uint32_t fields=ConfigAll) const;
private:
    BackendOptions options_;
    Nifm nifm_;
    TransportFactory factory_;
    mutable std::mutex mutex_;
    std::condition_variable initialized_;
    std::shared_ptr<Udp> transport_;
    uint64_t epoch_=0,initializing_epoch_=0;
    bool initializing_=false,stopped_=false;
    NifmStatus provider_call(NifmStatus (Nifm::*operation)());
};
}
