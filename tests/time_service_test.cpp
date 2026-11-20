// Actual production time service + independently source-derived SDK reader.
// Host wall time is checked; no game code, network, GPU or guest execution is required.
#include "ipc.h"
#include "time_sdk_consumer_fixture.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>

namespace fixture {
alignas(4096) std::array<uint8_t,4096> page{};
uint64_t tick=800ull*19200000;
bool lan=false;
std::shared_ptr<nx::KObject> handle;
std::map<std::string,nx::ServiceFactory>& registry() {static std::map<std::string,nx::ServiceFactory> value;return value;}
}
namespace nx {
std::atomic<uint64_t> g_frame{120};
uint64_t system_tick() {return fixture::tick;}
void register_service(const std::string& name,ServiceFactory factory) {fixture::registry()[name]=std::move(factory);}
uint32_t Service::handle(IpcCtx& c) {return cmds.at(c.cmd)(c);}
uint32_t handle_add(std::shared_ptr<KObject> object) {fixture::handle=std::move(object);return 7;}
std::shared_ptr<KSharedMemory> make_shared_memory(const std::string& name,uint64_t size) {
    if(size!=fixture::page.size())std::abort();
    auto value=std::make_shared<KSharedMemory>();value->name=name;value->size=size;
    value->host=reinterpret_cast<uintptr_t>(fixture::page.data());return value;
}
namespace lan {
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
bool active() {return fixture::lan;}
uint64_t epoch() {return 1700000000;}
uint32_t simulation_rate() {return 60;}
#endif
}
}
static void check(bool good,const char* message) {if(!good){std::fprintf(stderr,"%s\n",message);std::abort();}}
template<class T> T value(const nx::IpcCtx& c) {
    check(c.out.size()==sizeof(T),"unexpected IPC response size");T v;std::memcpy(&v,c.out.data(),sizeof(v));return v;
}
static void store32(size_t off,uint32_t v) {std::memcpy(fixture::page.data()+off,&v,sizeof(v));}
int main(int argc,char** argv) {
    fixture::lan=argc==2 && std::string(argv[1])=="--lan";
#if !defined(_WIN32) && !(defined(__linux__) && !defined(__ANDROID__))
    check(!fixture::lan,"LAN variant requires desktop runtime support");
#endif
    const auto before=std::time(nullptr);
    auto service=fixture::registry().at("time:u")();
    nx::IpcCtx request;request.cmd=20;
    check(service->handle(request)==0 && request.out_copy==std::vector<uint32_t>{7},"actual shared-memory handle");
    check(fixture::handle!=nullptr,"shared memory retained");
    request={};request.cmd=0;check(service->handle(request)==0 && request.out_objects.size()==1,"actual system clock child");
    auto clock=request.out_objects[0];request={};request.cmd=0;
    check(clock->handle(request)==0,"actual current time IPC");
    const auto ipc_now=value<int64_t>(request);
    check(ipc_now>0,"Unix epoch must not be zero");
    if(fixture::lan)check(ipc_now==1700000002,"LAN direct epoch/frame policy preserved");
    else check(ipc_now>=before && ipc_now<=std::time(nullptr),"offline host Unix epoch preserved");

    using namespace time_sdk_fixture;
    for(const auto& profile:profiles) {
        for(uint32_t parity:{0u,1u,6u,7u}) {
            store32(0xd0,parity);store32(0x38,parity);store32(0x80,parity);
            const auto user=current(profile,fixture::page,0x38,800000000000ll);
            const auto network=current(profile,fixture::page,0x80,800000000000ll);
            if(fixture::lan) {
                check(user.error==0xcc74 && network.error==0xcc74,
                      "legacy LAN shared SDK rejection intentionally unchanged; not a LAN clock fix");
                continue;
            }
            check(!user.error && !network.error && user.seconds==network.seconds,"both SDK/context/parity consumers accept source");
            if(fixture::lan) check(user.seconds==ipc_now,"initial shared Unix base matches actual IPC");
            else check(user.seconds>=before && user.seconds<=std::time(nullptr),"initial shared Unix base is real host epoch");
            const auto later=current(profile,fixture::page,0x38,802000000000ll);
            check(later.seconds==user.seconds+2,"neutral adjustment preserves host monotonic progress");
            const size_t record=0xd8+56*(parity&1);
            std::array<uint8_t,16> uuid;std::memcpy(uuid.data(),fixture::page.data()+record+40,16);
            std::memset(fixture::page.data()+record+40,0,16);
            check(current(profile,fixture::page,0x38,800000000000ll).error==0xcc74,"pre-fix zero record reproduces SDK source rejection");
            std::memcpy(fixture::page.data()+record+40,uuid.data(),16);
        }
    }
    // Legacy steady context is still coherent and is outside the new record.
    check(read<uint64_t>(fixture::page,0x10)!=0 && read<uint64_t>(fixture::page,0x18)!=0,"legacy steady UUID present");
    if(!fixture::lan)
        check(read<uint64_t>(fixture::page,0x10)==read<uint64_t>(fixture::page,0x100)
           && read<uint64_t>(fixture::page,0x18)==read<uint64_t>(fixture::page,0x108),"legacy and new steady UUID agree");
    check(read<uint64_t>(fixture::page,8)==0 && read<uint64_t>(fixture::page,0x20)==0,"legacy internal offsets preserved");
    if(fixture::lan) {
        // This patch fixes page ABI/source identity, not the pre-existing
        // frame-based IPC versus real-time shared-clock policy discrepancy.
        request={};request.cmd=0;fixture::tick+=2*19200000;
        check(clock->handle(request)==0 && value<int64_t>(request)==ipc_now,"LAN direct clock policy intentionally unchanged");
    }
    std::puts("actual time shared-page original/Product120 consumers passed");
}
