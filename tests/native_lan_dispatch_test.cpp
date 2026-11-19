#include "native_lan_dispatch.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>
using namespace nx;
using namespace nx::native_lan;
using Op=profile::Operation;
using Ret=profile::ReturnKind;
namespace {
void check(bool value,const char* message) {if(!value){std::cerr<<message<<'\n';std::exit(1);}}
class Memory:public GuestMemory {
public:
    std::vector<std::byte> bytes=std::vector<std::byte>(0x10000,std::byte{0xcc});
    bool validate(uint64_t address,size_t count,MemoryAccess) noexcept override {
        return address>=0x1000&&address<=bytes.size()&&count<=bytes.size()-size_t(address);
    }
    bool read(uint64_t address,std::span<std::byte> out) noexcept override {
        if(!validate(address,out.size(),MemoryAccess::Read))return false;
        std::copy_n(bytes.begin()+address,out.size(),out.begin());return true;
    }
    bool write(uint64_t address,std::span<const std::byte> in) noexcept override {
        if(!validate(address,in.size(),MemoryAccess::Write))return false;
        std::copy(in.begin(),in.end(),bytes.begin()+address);return true;
    }
    uint32_t value(uint64_t address) {uint32_t result;std::memcpy(&result,bytes.data()+address,4);return result;}
};
InterfaceInventory inventory() {
    HostInterface iface;iface.id="fixture-loopback";iface.up=true;iface.loopback=true;
    iface.address=0x7f000001;iface.mask=0xff000000;iface.gateway_known=iface.dns_known=true;
    iface.dns_servers={0x7f000001};return {NifmStatus::Ok,{iface}};
}
bool call(Backend& backend,Memory& memory,CPU& cpu,Op op,Ret ret,std::initializer_list<uint64_t> args={}) {
    size_t i=0;for(auto value:args)cpu.x[i++]=value;
    const CPU before=cpu;
    const bool handled=dispatch(backend,memory,cpu,op,ret);
    CPU expected=before;if(handled&&ret!=Ret::Void)expected.x[0]=cpu.x[0];
    check(std::memcmp(&expected,&cpu,sizeof cpu)==0,"dispatch preserves every register except declared return");
    return handled;
}
}
int main() {
    uint32_t parsed=0;
    check(parse_local_ipv4("192.168.2.1",parsed)&&parsed==0xc0a80201,"literal parser canonical byte order");
    for(auto text:{"1.2.3","1.2.3.4.5","01.2.3.4","256.2.3.4","1.2.3.-1","1.2.3.4 "})
        check(!parse_local_ipv4(text,parsed),"ambiguous/invalid IPv4 literal rejected");
    Backend backend({true,{},true},inventory);Memory memory;CPU cpu{};cpu.tpidrro=0x1000;
    constexpr uint64_t error_cell=0x11d8;
    check(call(backend,memory,cpu,Op::GetErrno,Ret::Signed32)&&cpu.x[0]==22,"uninitialized error getter returns SDK invalid state");
    const auto untouched=memory.value(error_cell);
    check(call(backend,memory,cpu,Op::SetErrno,Ret::Void,{99})&&memory.value(error_cell)==untouched,"uninitialized error setter does not alter TLS");
    check(call(backend,memory,cpu,Op::Initialize,Ret::Result32,{0,0,0,0})&&cpu.x[0]==0,"real owned socket capability initializes");
    check(call(backend,memory,cpu,Op::IsInitialized,Ret::Bool)&&cpu.x[0]==1,"initialized aliases agree");
    check(call(backend,memory,cpu,Op::InetPton,Ret::Signed32,{2,0x2000,0x2100})&&cpu.x[0]==0,"invalid literal returns false");
    const char text[]="127.0.0.1";std::memcpy(memory.bytes.data()+0x2000,text,sizeof text);
    check(call(backend,memory,cpu,Op::InetPton,Ret::Signed32,{2,0x2000,0x2100})&&cpu.x[0]==1,"literal conversion requires no external resolver");
    check(memory.bytes[0x2100]==std::byte{127}&&memory.bytes[0x2103]==std::byte{1},"in_addr uses network order");
    check(call(backend,memory,cpu,Op::InetNtop,Ret::Pointer,{2,0x2100,0x2200,16})&&cpu.x[0]==0x2200,"literal formatting returns guest output pointer");
    check(!std::memcmp(memory.bytes.data()+0x2200,text,sizeof text),"literal formatting roundtrip");
    check(call(backend,memory,cpu,Op::NifmInitialize,Ret::Result32)&&cpu.x[0]==0,"private provider establishes real context");
    call(backend,memory,cpu,Op::NifmSetLocalMode,Ret::Void,{0});
    call(backend,memory,cpu,Op::NifmSubmitAndWait,Ret::Void);
    check(call(backend,memory,cpu,Op::NifmAvailable,Ret::Bool)&&!cpu.x[0],"internet request stays offline");
    call(backend,memory,cpu,Op::NifmSetLocalMode,Ret::Void,{1});
    call(backend,memory,cpu,Op::NifmSubmitAndWait,Ret::Void);
    check(call(backend,memory,cpu,Op::NifmAvailable,Ret::Bool)&&cpu.x[0],"local request becomes available");
    check(call(backend,memory,cpu,Op::NifmIpConfig,Ret::Result32,{0x3000,0x3004,0x3008,0x300c,0x3010})&&!cpu.x[0],"five-field real fixture config");
    check(memory.bytes[0x3000]==std::byte{127}&&memory.bytes[0x3003]==std::byte{1}&&memory.bytes[0x3004]==std::byte{255},"IP and subnet encode network bytes");
    const auto before=memory.bytes;
    call(backend,memory,cpu,Op::NifmIpConfig,Ret::Result32,{0x3000,0x3004,UINT64_MAX,0x300c,0x3010});
    check(cpu.x[0]&&memory.bytes==before,"one invalid output cannot partially overwrite config");
    check(call(backend,memory,cpu,Op::Socket,Ret::Signed32,{2,2,17})&&int64_t(cpu.x[0])>=0,"raw socket dispatch creates owned descriptor");
    const auto fd=cpu.x[0];auto address=ipv4_address(0x7f000001,0);address[0]=std::byte{0};
    memory.write(0x4000,address);
    check(call(backend,memory,cpu,Op::Bind,Ret::Signed32,{fd,0x4000,16})&&!cpu.x[0],"raw zero-sin_len reaches real loopback bind");
    call(backend,memory,cpu,Op::NifmCancel,Ret::Void);
    call(backend,memory,cpu,Op::Socket,Ret::Signed32,{2,2,17});
    check(cpu.x[0]==UINT64_MAX&&memory.value(error_cell)==NetworkDown,"request cancellation gates new network operations");
    check(call(backend,memory,cpu,Op::Close,Ret::Signed32,{fd})&&!cpu.x[0],"offline cleanup can close existing descriptors");
    call(backend,memory,cpu,Op::Finalize,Ret::Result22);
    check(!cpu.x[0]&&!backend.socket_initialized(),"C finalize returns masked Result success and releases provider");
    call(backend,memory,cpu,Op::GetErrno,Ret::Signed32);
    check(cpu.x[0]==22,"all error aliases restore uninitialized semantics after finalize");
    Backend disabled({},inventory);
    call(disabled,memory,cpu,Op::Initialize,Ret::Result22,{0,0,0,0});
    check(cpu.x[0]&&cpu.x[0]<=0x3fffff,"failed C initialize retains Result22 contract");
    {
        Backend racing({true,{},true},inventory);
        const std::array<std::byte,4> previous_errno{std::byte{99},std::byte{0},std::byte{0},std::byte{0}};
        memory.write(error_cell,previous_errno);
        std::atomic<bool> ready=false,done=false,broken=false;
        std::thread finalizer([&] {
            while(!ready.load())std::this_thread::yield();
            for(unsigned i=0;i<300;++i) {racing.initialize_sockets();racing.finalize_sockets();}
            done=true;
        });
        CPU reader{};reader.tpidrro=0x1000;ready=true;
        for(unsigned i=0;i<10000||!done.load();++i) {
            reader.x[0]=99;
            if(!dispatch(racing,memory,reader,Op::SetErrno,Ret::Void)||reader.x[0]!=99)broken=true;
            if(!dispatch(racing,memory,reader,Op::GetErrno,Ret::Signed32)||(reader.x[0]!=99&&reader.x[0]!=22))broken=true;
        }
        finalizer.join();
        check(!broken,"concurrent finalize cannot clobber void errno setter or synthesize getter -1");
    }
    std::cout<<"Native dispatch: lifecycle, offline/local gating, byte order, output prevalidation and owned descriptor cleanup passed\n";
}
