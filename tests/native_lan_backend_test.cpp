#include "native_lan_backend.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
using namespace nx::native_lan;
namespace {
void check(bool ok,const char* why) { if(!ok) { std::fprintf(stderr,"native backend: %s\n",why);std::exit(1); } }
InterfaceInventory fixture() {
    HostInterface item;item.id="loopback-fixture";item.address=0x7f000001;item.mask=0xff000000;
    item.up=true;item.loopback=true;item.gateway_known=item.dns_known=true;
    return {NifmStatus::Ok,{item}};
}
BackendOptions enabled() { return {true,0x7f000001,true}; }
int open_loopback(const std::shared_ptr<Udp>& udp) {
    const auto created=udp->socket(2,2,17);check(bool(created),"create admitted UDP descriptor");
    const int fd=int(created.value);check(bool(udp->bind(fd,ipv4_address(0x7f000001,0))),"bind only loopback");return fd;
}
}
int main() {
#ifdef _WIN32
    check(SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS)!=0,"BelowNormal CPU test");
#endif
    unsigned enumerations=0,factories=0;
    {
        Backend off({},[&] { ++enumerations;return fixture(); },[&] { ++factories;return std::make_shared<Udp>(false); });
        check(off.initialize_sockets().error==NetworkDown && !off.socket_initialized(),"default disabled initialization rejected");
        check(off.initialize_nifm()==NifmStatus::Disabled && off.set_local_mode(true)==NifmStatus::Disabled && off.submit_request()==NifmStatus::Disabled,"disabled provider cannot become available");
        check(!off.acquire() && !off.acquire(BackendAccess::Cleanup),"default disabled access rejected");
        off.finalize_sockets();off.stop();off.stop();
        check(off.initialize_sockets().error==Interrupted && off.initialize_nifm()==NifmStatus::Cancelled &&
            !off.network_available(),"disabled stop is irreversible without provider work");
        check(enumerations==0 && factories==0,"default-off performs no enumeration/factory/socket creation");
    }
    {
        Backend failed(enabled(),[&] { ++enumerations;return fixture(); },[&] { ++factories;return std::make_shared<Udp>(false); });
        check(failed.initialize_sockets().error==NetworkDown && !failed.socket_initialized(),"disabled injected transport probe cannot invent readiness");
        check(failed.acquire().error==Invalid,"preinitialized transport unavailable");
        check(enumerations==0 && factories==1,"capability initialization does not enumerate interfaces");
        Backend absent(enabled(),fixture,[] { return std::shared_ptr<Udp>{}; });
        check(absent.initialize_sockets().error==NoBuffers && !absent.socket_initialized(),"null factory result fails initialization");
        Backend throwing(enabled(),fixture,[]() -> std::shared_ptr<Udp> { throw std::runtime_error("fixture failure"); });
        check(throwing.initialize_sockets().error==Io && !throwing.socket_initialized(),"factory exception cannot fabricate provider context");
    }
    {
        Backend network_first(enabled(),fixture,[] { return std::make_shared<Udp>(false); });
        check(network_first.initialize_nifm()==NifmStatus::Ok && network_first.set_local_mode(true)==NifmStatus::Ok &&
            network_first.submit_request()==NifmStatus::Ok,"Nifm setup precedes socket capability");
        check(network_first.network_available() && !network_first.socket_initialized() && !network_first.acquire(),
            "cached Nifm availability is independent of uninitialized socket provider");
        network_first.cancel_request();check(!network_first.network_available(),"cached availability observes request cancel");
    }
    {
        // A finalization racing an arbitrary injected factory must invalidate
        // that candidate and cannot resurrect initialization on late return.
        std::promise<void> entered,release;auto gate=release.get_future().share();
        Backend pending(enabled(),fixture,[&] { entered.set_value();gate.wait();return std::make_shared<Udp>(false); });
        Result result;
        std::thread initialize([&] { result=pending.initialize_sockets(); });
        entered.get_future().wait();pending.finalize_sockets();release.set_value();initialize.join();
        check(result.error==Interrupted && !pending.socket_initialized(),"late initialized candidate rejected after finalize epoch");
    }
    {
        unsigned probes=0,scans=0;
        Backend backend(enabled(),[&] { ++scans;return fixture(); },[&] { ++probes;return std::make_shared<Udp>(true); });
        check(bool(backend.initialize_sockets()) && backend.socket_initialized(),"real host create/close probe establishes owned context");
        check(probes==1 && scans==0 && !backend.acquire(),"socket provider readiness precedes local network availability");
        check(backend.acquire(static_cast<BackendAccess>(99)).error==Invalid,"invalid access kind cannot bypass local availability gate");
        check(bool(backend.initialize_sockets()) && probes==1,"repeated initialize retains same ready provider");
        check(backend.initialize_nifm()==NifmStatus::Ok && !backend.acquire(),"initialized interface context does not imply local availability");
        check(backend.submit_request()==NifmStatus::InternetOffline && !backend.acquire(),"internet mode cannot obtain transport");
        check(backend.set_local_mode(true)==NifmStatus::Ok && !backend.acquire(),"explicit local mode still needs request");
        check(backend.submit_request()==NifmStatus::Ok,"explicit local request selects real loopback fixture");
        auto admitted=backend.acquire();check(bool(admitted),"available local mode admits one transport call");
        const unsigned cached=scans;
        for(unsigned i=0;i<100;++i)check(bool(backend.acquire()),"cached data-path gate");
        check(scans==cached,"socket data path never enumerates interfaces");
        const int fd=open_loopback(admitted.transport);
        check(backend.cancel_request()==NifmStatus::Ok && !backend.acquire() && backend.socket_initialized(),"request cancellation gates new network calls without losing socket context");
        auto cleanup=backend.acquire(BackendAccess::Cleanup);Address local{};
        check(bool(cleanup) && bool(cleanup.transport->local_address(fd,local)),"descriptor cleanup/query remains valid offline");
        check(backend.set_local_mode(false)==NifmStatus::Ok && backend.submit_request()==NifmStatus::InternetOffline && !backend.acquire(),"return to internet mode never enables packets");
        check(backend.set_local_mode(true)==NifmStatus::Ok && backend.submit_request()==NifmStatus::Ok && bool(backend.acquire()),"mode/request recovery reuses live uncancelled provider");
        std::atomic<bool> polling{false},done{false};Result polled;
        std::thread worker([ref=admitted.transport,fd,&polling,&done,&polled] {
            PollFd entry{fd,Readable};polling=true;polled=ref->poll({&entry,1},1000);done=true;
        });
        while(!polling.load())std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        check(!done,"loopback poll waits with no incoming packets");
        check(backend.socket_initialized(),"backend state access is not locked behind Poll");
        const auto start=std::chrono::steady_clock::now();backend.finalize_sockets();worker.join();
        check(polled.error==Interrupted && std::chrono::steady_clock::now()-start<std::chrono::milliseconds(500),"finalize cancels in-flight shared transport Poll promptly");
        check(!backend.socket_initialized() && !backend.acquire() && !backend.acquire(BackendAccess::Cleanup),"finalize invalidates new and cleanup acquisitions");
        check(admitted.transport->socket(2,2,17).error==Interrupted && bool(admitted.transport->close(fd)),"old shared ref stays alive for close but cannot create more sockets");
        check(bool(backend.initialize_sockets()) && probes==2,"reinitialize performs new real capability probe");
        auto fresh=backend.acquire();check(bool(fresh) && fresh.transport!=admitted.transport,"reinitialize never resets/recycles transport beneath old Poll reference");
        const int fresh_fd=open_loopback(fresh.transport);check(bool(fresh.transport->close(fresh_fd)),"new provider descriptor cleanup");
    }
    {
        std::atomic<unsigned> probes{0};std::promise<void> entered,release;auto gate=release.get_future().share();
        Backend backend(enabled(),fixture,[&] { ++probes;entered.set_value();gate.wait();return std::make_shared<Udp>(true); });
        Result first,second;
        std::thread one([&] { first=backend.initialize_sockets(); });entered.get_future().wait();
        std::thread two([&] { second=backend.initialize_sockets(); });release.set_value();one.join();two.join();
        check(bool(first) && bool(second) && probes==1 && backend.socket_initialized(),"concurrent initialize shares one successful capability probe");
    }
    {
        std::promise<void> entered,release;auto gate=release.get_future().share();
        unsigned factories_after_stop=0;
        Backend stopped(enabled(),fixture,[&] { ++factories_after_stop;entered.set_value();gate.wait();return std::make_shared<Udp>(false); });
        Result initialized;
        std::thread initializer([&] { initialized=stopped.initialize_sockets(); });
        entered.get_future().wait();stopped.stop();
        check(stopped.initialize_sockets().error==Interrupted,"stop rejects new initialization while old factory is blocked");
        release.set_value();initializer.join();
        check(initialized.error==Interrupted && factories_after_stop==1 && !stopped.socket_initialized(),"blocked factory cannot publish transport after irreversible stop");
        stopped.finalize_sockets();stopped.stop();
        check(stopped.initialize_sockets().error==Interrupted,"Finalize and repeated stop cannot undo irreversible stop");
    }
    {
        unsigned scans=0;
        Backend stopped(enabled(),[&] { ++scans;return fixture(); });
        check(bool(stopped.initialize_sockets()) && stopped.initialize_nifm()==NifmStatus::Ok &&
            stopped.set_local_mode(true)==NifmStatus::Ok && stopped.submit_request()==NifmStatus::Ok,"ready provider for irreversible shutdown");
        auto ref=stopped.acquire();check(bool(ref),"admit bounded call before stop");
        const int fd=open_loopback(ref.transport);
        Result polled;std::atomic<bool> polling{false},done{false};
        std::thread worker([&] { PollFd entry{fd,Readable};polling=true;polled=ref.transport->poll({&entry,1},1000);done=true; });
        while(!polling.load())std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));check(!done,"ready Poll remains waiting before stop");
        stopped.stop();worker.join();
        check(polled.error==Interrupted && !stopped.socket_initialized() && !stopped.network_available(),"stop cancels ready Poll and cached availability");
        check(!stopped.acquire(BackendAccess::Cleanup) && bool(ref.transport->close(fd)),"no new cleanup admission after stop; admitted shared ref retains safe close");
        const auto before=scans;
        check(stopped.initialize_nifm()==NifmStatus::Cancelled && stopped.set_local_mode(true)==NifmStatus::Cancelled &&
            stopped.submit_request()==NifmStatus::Cancelled && stopped.cancel_request()==NifmStatus::Cancelled &&
            stopped.refresh()==NifmStatus::Cancelled && stopped.config().status==NifmStatus::Cancelled,
            "all stopped Nifm lifecycle/config operations are gated");
        const auto snapshot=stopped.snapshot();
        check(scans==before && !snapshot.requested && !snapshot.available,"stopped lifecycle never enumerates or recreates request");
        stopped.stop();stopped.finalize_sockets();check(stopped.initialize_sockets().error==Interrupted,"ready stop is idempotent and irreversible");
    }
    {
        std::promise<void> entered,release;auto gate=release.get_future().share();
        std::atomic<bool> block{false};std::atomic<unsigned> scans{0};
        Backend stopped(enabled(),[&] {
            ++scans;if(block.load()) { entered.set_value();gate.wait(); }return fixture();
        },[] { return std::make_shared<Udp>(false); });
        check(stopped.initialize_nifm()==NifmStatus::Ok && stopped.set_local_mode(true)==NifmStatus::Ok,"provider initialized before blocked request");
        block=true;NifmStatus result=NifmStatus::Ok;
        std::thread request([&] { result=stopped.submit_request(); });
        entered.get_future().wait();stopped.stop();
        check(!stopped.network_available() && !stopped.snapshot().requested,"stop immediately clears availability during admitted enumeration");
        release.set_value();request.join();
        check(result==NifmStatus::Cancelled && !stopped.network_available() && !stopped.snapshot().requested,"late provider refresh cannot resurrect availability after stop");
        const auto before=scans.load();check(stopped.refresh()==NifmStatus::Cancelled && scans==before,"stopped refresh performs no further enumeration");
    }
    std::puts("Native backend: default-off/init failures, honest local gating, offline cleanup, shared Poll cancellation, finalize epochs and reinitialize passed (owned backend only)");
}
