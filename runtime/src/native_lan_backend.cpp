#include "native_lan_backend.h"
#include <new>
#include <utility>

namespace nx::native_lan {
namespace {
Result failure(int error) { return {-1,error,false}; }
}
Backend::Backend(BackendOptions options,Nifm::Enumerator enumerate,TransportFactory factory)
    :options_(options),nifm_({options.enabled,options.address,options.allow_loopback_fixture},std::move(enumerate)),
     factory_(factory?std::move(factory):[] { return std::make_shared<Udp>(true); }) {}
Backend::~Backend() { stop(); }
Result Backend::initialize_sockets() {
    uint64_t epoch;
    {
        std::unique_lock lock(mutex_);
        initialized_.wait(lock,[&] { return stopped_ || !initializing_ || bool(transport_); });
        if(stopped_)return failure(Interrupted);
        if(!options_.enabled)return failure(NetworkDown);
        if(transport_)return {0,0,false};
        epoch=epoch_;initializing_=true;initializing_epoch_=epoch;
    }
    std::shared_ptr<Udp> candidate;
    Result result=failure(NoBuffers);
    try {
        candidate=factory_();
        if(candidate) {
            const auto probe=candidate->socket(2,2,17);
            result=probe?candidate->close(int(probe.value)):probe;
        }
    } catch(const std::bad_alloc&) { result=failure(NoBuffers); }
      catch(...) { result=failure(Io); }
    bool published=false;
    {
        std::lock_guard lock(mutex_);
        if(stopped_ || epoch_!=epoch)result=failure(Interrupted);
        else if(result && candidate) { transport_=candidate;published=true; }
        if(initializing_ && initializing_epoch_==epoch)initializing_=false;
    }
    initialized_.notify_all();
    if(candidate && !published)candidate->cancel();
    return result;
}
void Backend::finalize_sockets() noexcept {
    std::shared_ptr<Udp> old;
    {
        std::lock_guard lock(mutex_);
        ++epoch_;old=std::move(transport_);initializing_=false;
    }
    initialized_.notify_all();
    if(old)old->cancel(); // shared refs keep OS descriptors alive under Poll.
}
void Backend::stop() noexcept {
    std::shared_ptr<Udp> old;
    {
        std::lock_guard lock(mutex_);
        if(stopped_)return;
        stopped_=true;++epoch_;old=std::move(transport_);initializing_=false;
        nifm_.cancel_request(); // cached state only; also invalidates in-flight refresh epoch
    }
    initialized_.notify_all();
    if(old)old->cancel();
}
bool Backend::socket_initialized() const {
    std::lock_guard lock(mutex_);return bool(transport_);
}
bool Backend::network_available() const noexcept {
    std::lock_guard lock(mutex_);return !stopped_ && nifm_.available();
}
BackendTransport Backend::acquire(BackendAccess access) const {
    std::lock_guard lock(mutex_);
    if(stopped_)return {{},Interrupted};
    if(!options_.enabled)return {{},NetworkDown};
    if(access!=BackendAccess::Network && access!=BackendAccess::Cleanup)return {{},Invalid};
    if(!transport_)return {{},Invalid};
    if(access==BackendAccess::Network && !nifm_.available())return {{},NetworkDown};
    return {transport_,0};
}
NifmStatus Backend::provider_call(NifmStatus (Nifm::*operation)()) {
    { std::lock_guard lock(mutex_);if(stopped_)return NifmStatus::Cancelled; }
    // Already-admitted enumeration may finish after stop, but cannot restore a
    // public request/availability result. Never hold backend mutex across OS I/O.
    const auto result=(nifm_.*operation)();
    std::lock_guard lock(mutex_);
    if(stopped_) { nifm_.cancel_request();return NifmStatus::Cancelled; }
    return result;
}
NifmStatus Backend::initialize_nifm() { return provider_call(&Nifm::initialize); }
NifmStatus Backend::set_local_mode(bool local) {
    std::lock_guard lock(mutex_);
    return stopped_?NifmStatus::Cancelled:nifm_.set_local_mode(local);
}
NifmStatus Backend::submit_request() { return provider_call(&Nifm::submit_request); }
NifmStatus Backend::cancel_request() {
    std::lock_guard lock(mutex_);
    return stopped_?NifmStatus::Cancelled:nifm_.cancel_request();
}
NifmStatus Backend::refresh() { return provider_call(&Nifm::refresh); }
NifmSnapshot Backend::snapshot() const {
    std::lock_guard lock(mutex_);auto state=nifm_.snapshot();
    if(stopped_) {state.available=false;state.requested=false;}
    return state;
}
NifmConfigResult Backend::config(uint32_t fields) const {
    std::lock_guard lock(mutex_);
    return stopped_?NifmConfigResult{NifmStatus::Cancelled}:nifm_.config(fields);
}
}
