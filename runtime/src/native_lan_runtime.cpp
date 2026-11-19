#include "native_lan_runtime.h"
#include <nx/runtime.h>
#include <cstdlib>
#include <cstring>
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
#include "native_lan_runtime.h"
#include "native_lan_dispatch.h"
#include "native_lan_memory.h"
#include "netplay_game.h"
#include "platform.h"
#include <nx/game.h>
#include <nx/gamelib.h>
#include <nx/runtime.h>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>

namespace nx::native_lan {
namespace {
using Op=profile::Operation;
using Ret=profile::ReturnKind;
constexpr size_t kOperations=size_t(Op::Count),kReturns=8;
const char* name(Op op) {
    static constexpr const char* names[]={"initialize","initialize_config","finalize","is_initialized",
        "socket","bind","send_to","recv_from","poll","close","get_errno","set_errno","set_option",
        "get_option","shutdown","get_sock_name","recv","send","read","write","fcntl","unsupported_socket",
        "resolve_failure","host_resolve_failure","resolver_get_option","resolver_set_option","inet_pton","inet_ntop",
        "nifm_initialize","nifm_local_mode","nifm_submit","nifm_submit_wait","nifm_cancel","nifm_available",
        "nifm_on_hold","nifm_request_error","nifm_ip_config","nifm_proxy_unavailable","preserved"};
    static_assert(std::size(names)==kOperations);
    return names[size_t(op)];
}
struct Count {std::atomic<uint64_t> calls{0},failures{0},total_ns{0};};
struct Context {
    Backend backend;
    std::array<Count,kOperations> counts;
    std::atomic<uint64_t> emitted{0},send_return_bytes{0},receive_return_bytes{0};
    std::atomic<uint64_t> available_yes{0},available_no{0},local_yes{0},local_no{0},last_status_refresh{0};
    explicit Context(BackendOptions options):backend(options) {}
};
std::atomic_flag shutdown_started=ATOMIC_FLAG_INIT;
std::atomic<Context*> context{nullptr}; // Published before any guest thread starts; process lifetime.
bool failed(const CPU& cpu,Ret result) {
    switch(result) {
    case Ret::Signed32:return int32_t(cpu.x[0])<0;
    case Ret::Signed64:return int64_t(cpu.x[0])<0;
    case Ret::Result32:return uint32_t(cpu.x[0])!=0;
    case Ret::Result22:return (cpu.x[0]&0x3fffff)!=0;
    case Ret::Pointer:return !cpu.x[0];
    default:return false; // Availability=false is not itself an operation error.
    }
}
void invoke(CPU& cpu,Op op,Ret result) {
    auto* state=context.load(std::memory_order_acquire);
    if(!state)fatal("native LAN handler invoked before its checked startup transaction");
    const uint64_t started=plat::monotonic_ns();
    const bool local=op==Op::NifmSetLocalMode&&uint8_t(cpu.x[0]);
    try {
        if(op==Op::NifmAvailable) {
            auto last=state->last_status_refresh.load(std::memory_order_relaxed);
            if(started>=last && started-last>=2'000'000'000ull && state->last_status_refresh.compare_exchange_strong(last,started,std::memory_order_relaxed))
                state->backend.refresh(); // Status boundary only; packet operations never enumerate interfaces.
        }
        RuntimeGuestMemory memory;
        if(!dispatch(state->backend,memory,cpu,op,result))
            fatal("native LAN stopped after an unrecoverable guest mapping failure (%s); no fallback was attempted",name(op));
    } catch(const std::exception& error) {
        fatal("native LAN operation %s stopped: %s",name(op),error.what());
    }
    if(op==Op::NifmAvailable)(cpu.x[0]?state->available_yes:state->available_no).fetch_add(1,std::memory_order_relaxed);
    if(op==Op::NifmSetLocalMode)(local?state->local_yes:state->local_no).fetch_add(1,std::memory_order_relaxed);
    auto& counter=state->counts[size_t(op)];
    const auto call=counter.calls.fetch_add(1,std::memory_order_relaxed)+1;
    const bool error=op==Op::ResolveFailure?cpu.x[0]!=0:failed(cpu,result);
    if(error)counter.failures.fetch_add(1,std::memory_order_relaxed);
    counter.total_ns.fetch_add(plat::monotonic_ns()-started,std::memory_order_relaxed);
    if(!error) {
        if(op==Op::SendTo||op==Op::Send||op==Op::Write)state->send_return_bytes.fetch_add(cpu.x[0],std::memory_order_relaxed);
        if(op==Op::RecvFrom||op==Op::Recv||op==Op::Read)state->receive_return_bytes.fetch_add(cpu.x[0],std::memory_order_relaxed);
    }
    if((call<=4||!(call&(call-1)))&&state->emitted.fetch_add(1,std::memory_order_relaxed)<256)
        log("native LAN owned provider: op=%s call=%llu failed=%u availability_or_local_mode=%u",name(op),
            (unsigned long long)call,unsigned(error),unsigned(op==Op::NifmAvailable?bool(cpu.x[0]):local));
}
template<size_t Index>void thunk(CPU& cpu) {invoke(cpu,Op(Index/kReturns),Ret(Index%kReturns));}
template<size_t... Index>constexpr auto thunks(std::index_sequence<Index...>) {
    return std::array<Fn,sizeof...(Index)>{thunk<Index>...};
}
constexpr auto handlers=thunks(std::make_index_sequence<kOperations*kReturns>{});
profile::Lookup lookup() {
    return {
        [](std::string_view alias){return hle_lookup(std::string(alias).c_str());},
        [](std::string_view alias){return hle_thunk_cached(std::string(alias).c_str());},
        [](Op operation,Ret result)->Fn {
            if(operation==Op::PreserveOriginal||operation==Op::Count||result==Ret::Preserved)return nullptr;
            return handlers[size_t(operation)*kReturns+size_t(result)];
        }};
}
class Commit final:public profile::Committer {
public:
    bool commit(std::span<const profile::Binding> bindings) override {
        std::vector<std::string> names;names.reserve(bindings.size());
        for(const auto& binding:bindings)names.emplace_back(binding.name);
        std::vector<HleBinding> replacements;replacements.reserve(bindings.size());
        for(size_t i=0;i<bindings.size();++i)replacements.push_back({names[i].c_str(),bindings[i].replacement});
        // Allocation/registry validation finishes before publication. Following
        // SDK stores cannot fail, and no guest can observe a partial transaction.
        if(!hle_replace_before_relocations(replacements))return false;
        for(const auto& binding:bindings)if(binding.sdk_entry)*binding.sdk_entry=binding.replacement;
        return true;
    }
};
void finish() noexcept {
    auto* state=context.load(std::memory_order_acquire);
    if(!state||shutdown_started.test_and_set())return;
    state->backend.stop();
    try {
        const auto snapshot=state->backend.snapshot();
        const auto root=plat::data_root();
        const auto data=std::filesystem::path(reinterpret_cast<const char8_t*>(root.c_str()));
        std::ofstream out(data/"native-lan-stats.json",std::ios::binary|std::ios::trunc);
        out<<"{\n\"schema\":1,\"backend\":\"Product120-private-udp-v1\",\"operations\":{";
        for(size_t i=0;i<kOperations;++i) {
            if(i)out<<',';
            const auto& counter=state->counts[i];
            out<<'\"'<<name(Op(i))<<"\":{\"calls\":"<<counter.calls.load()
                <<",\"failures\":"<<counter.failures.load()<<",\"duration_ns\":"<<counter.total_ns.load()<<'}';
        }
        out<<"},\"nifm_last_cached_snapshot\":{\"initialized\":"<<snapshot.initialized
            <<",\"local_mode\":"<<snapshot.local_mode<<",\"requested\":"<<snapshot.requested
            <<",\"available\":"<<snapshot.available<<",\"selection_status\":"<<int(snapshot.selection.status)
            <<",\"candidates\":"<<snapshot.selection.candidates.size()<<"},\"available_true_calls\":"<<state->available_yes.load()
            <<",\"available_false_calls\":"<<state->available_no.load()<<",\"local_true_calls\":"<<state->local_yes.load()
            <<",\"local_false_calls\":"<<state->local_no.load()<<",\"send_return_bytes\":"<<state->send_return_bytes.load()
            <<",\"receive_return_bytes_including_peeks\":"<<state->receive_return_bytes.load()
            <<",\"scope\":\"Non-transactional host-provider call counters; durations include waits, not RTT; return bytes are not wire traffic or world-state verification\"}\n";
        if(!out)log("native LAN: diagnostic export failed");
    } catch(...) {log("native LAN: diagnostic export failed");}
}
}
bool active() noexcept {return context.load(std::memory_order_acquire)!=nullptr;}
void shutdown() noexcept {finish();}
void install_before_relocations() {
    const char* flag=std::getenv("NX_NATIVE_LAN");
    if(!flag||!std::strcmp(flag,"0"))return;
    if(std::strcmp(flag,"1"))fatal("NX_NATIVE_LAN must be 0 or 1");
    if(std::strcmp(game::kInfo.id,"wonder"))fatal("Native LAN has a verified Wonder Product120 profile only");
    if(lan::active())fatal("Choose either game-native LAN or shared-input LAN for this process");
    if(!RuntimeGuestMemory::supported())fatal("Native LAN guest-memory pinning currently supports Windows only");
    if(active())fatal("Native LAN startup may only be installed once");
    BackendOptions options;options.enabled=true;
    if(const char* address=std::getenv("NX_NATIVE_LAN_ADDRESS");address&&*address) {
        uint32_t value;
        if(!parse_local_ipv4(address,value))fatal("NX_NATIVE_LAN_ADDRESS must be a literal IPv4 address");
        options.address=value;
    }
    const auto& game=game_lib();
    if(!game.main||!game.sdk)fatal("Native LAN requires both module metadata records");
    const auto* main=module_at(game.main->base);
    const auto* sdk=module_at(game.sdk->base);
    if(!main||!sdk)fatal("Native LAN installation requires both mapped modules");
    const auto view=[](const LoadedModule& module) {
        return profile::ModuleView{module.info,
            {reinterpret_cast<const uint8_t*>(module.base),size_t(module.info->text_size)},
            {module.table,size_t(module.info->text_size/4)}};
    };
    const auto query=lookup();
    const auto plan=profile::prepare(true,profile::product120(),view(*main),view(*sdk),profile::Phase::BeforeRelocations,query);
    if(plan.status!=profile::Status::Ready)fatal("Native LAN profile rejected: %s",plan.error.c_str());
    auto owned=std::make_unique<Context>(options);
    Commit commit;
    if(!profile::apply(plan,profile::Phase::BeforeRelocations,query,commit))fatal("Native LAN binding transaction rejected");
    context.store(owned.release(),std::memory_order_release);
    std::atexit(finish);
    log("native LAN: installed %zu checked owned-provider aliases before relocation; internet mode stays offline; no game-mode forcing",plan.bindings.size());
}
}

#else
namespace nx::native_lan {
bool active() noexcept {return false;}
void shutdown() noexcept {}
void install_before_relocations() {
    if(const char* flag=std::getenv("NX_NATIVE_LAN");flag&&std::strcmp(flag,"0"))
        fatal("The experimental native LAN provider is not available on this platform");
}
}
#endif
