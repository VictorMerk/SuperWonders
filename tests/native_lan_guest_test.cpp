#include "native_lan_guest.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>
#include <type_traits>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
using namespace nx;
using namespace nx::native_lan;
namespace {
void check(bool ok,const char* message) {
    if(!ok) { std::fprintf(stderr,"native guest: %s\n",message);std::exit(1); }
}
class Memory final:public GuestMemory {
public:
    static constexpr uint64_t base=0x1000;
    std::vector<std::byte> bytes=std::vector<std::byte>(0x20000,std::byte{0xcc});
    uint64_t deny_read=0,deny_write=0,fail_write=0,fail_read=0;
    bool validate(uint64_t a,size_t size,MemoryAccess access) noexcept override {
        if(a<base || a-base>bytes.size() || size>bytes.size()-size_t(a-base))return false;
        const auto includes=[&](uint64_t denied) { return denied && a<=denied && denied-a<size; };
        return !(access!=MemoryAccess::Write && includes(deny_read)) &&
               !(access!=MemoryAccess::Read && includes(deny_write));
    }
    bool read(uint64_t a,std::span<std::byte> out) noexcept override {
        if(a==fail_read || !validate(a,out.size(),MemoryAccess::Read))return false;
        std::copy_n(bytes.begin()+size_t(a-base),out.size(),out.begin());return true;
    }
    bool write(uint64_t a,std::span<const std::byte> in) noexcept override {
        if(a==fail_write || !validate(a,in.size(),MemoryAccess::Write))return false;
        std::copy(in.begin(),in.end(),bytes.begin()+size_t(a-base));return true;
    }
    void put32(uint64_t a,uint32_t value) {
        for(unsigned i=0;i<4;++i)bytes.at(size_t(a-base+i))=std::byte((value>>(8*i))&255);
    }
    uint32_t get32(uint64_t a) const {
        uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::to_integer<uint32_t>(bytes.at(size_t(a-base+i)))<<(8*i);
        return value;
    }
    void put(uint64_t a,std::span<const std::byte> data) { check(write(a,data),"prepare fake mapped guest memory"); }
};
CPU context(uint64_t tls) {
    CPU c{};
    for(unsigned i=0;i<32;++i) { c.x[i]=0x5555000000000000ull+i;c.v[i].u64[0]=i+70;c.v[i].u64[1]=i+100; }
    c.nf=1;c.zf=2;c.cf=3;c.vf=4;c.fpcr=31;c.fpsr=32;c.tpidr=0x1234;c.tpidrro=tls;
    c.excl_addr=99;c.excl_val=100;c.excl_val_hi=101;return c;
}
bool call(GuestAdapter& adapter,CPU& cpu,GuestCall operation,std::initializer_list<uint64_t> args={}) {
    size_t i=0;for(auto value:args)cpu.x[i++]=value;
    CPU before=cpu;
    const bool handled=adapter.invoke(cpu,operation);
    if(handled)before.x[0]=cpu.x[0];
    check(std::memcmp(&before,&cpu,sizeof cpu)==0,"ABI changes only x0 on completion; no registers on adapter failure");
    return handled;
}
uint64_t negative(int value) { return uint64_t(int64_t(value)); }
void guest_error(GuestAdapter& adapter,CPU& cpu,Memory& memory,GuestCall operation,int error,std::initializer_list<uint64_t> args) {
    check(call(adapter,cpu,operation,args) && cpu.x[0]==~uint64_t(0),"guest scalar failure returns -1");
    check(memory.get32(cpu.tpidrro+GuestAdapter::kErrnoOffset)==uint32_t(error),"guest TLS errno written");
}
int socket(GuestAdapter& adapter,CPU& cpu) {
    check(call(adapter,cpu,GuestCall::Socket,{2,2,17}) && int64_t(cpu.x[0])>=0,"create guest UDP socket");return int(cpu.x[0]);
}
Address bind(GuestAdapter& adapter,CPU& cpu,Memory& memory,Udp& udp,int fd,uint64_t guest_address) {
    auto raw=ipv4_address(0x7f000001,0);raw[0]=std::byte{0};memory.put(guest_address,raw);
    check(call(adapter,cpu,GuestCall::Bind,{uint64_t(fd),guest_address,16}) && cpu.x[0]==0,"raw Wonder sin_len0 bind accepted");
    check(memory.bytes[guest_address-Memory::base]==std::byte{0},"normalization must not mutate guest sockaddr");
    Address result{};check(bool(udp.local_address(fd,result)),"query loopback ephemeral port");
    uint32_t ip;uint16_t port;check(decode_ipv4(result,ip,port) && ip==0x7f000001 && port,"network-order IPv4 roundtrip");
    return result;
}
void wait(Udp& udp,int fd) { PollFd p{fd,Readable};check(udp.poll({&p,1},1000).value==1,"loopback receive readiness"); }
}
int main() {
    static_assert(!std::is_constructible_v<GuestAdapter,Udp&,GuestMemory&>);
    // This fake fixture asserts an already established backend context. It
    // does not test/implement SDK Initialize/client service or errno aliases.
#ifdef _WIN32
    check(SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS)!=0,"tests run BelowNormal");
#endif
    Memory memory;CPU a=context(0x1000),b=context(0x2000);
    const uint64_t ea=a.tpidrro+GuestAdapter::kErrnoOffset,eb=b.tpidrro+GuestAdapter::kErrnoOffset;
    memory.put32(ea,77);memory.put32(eb,88);
    {
        Udp disabled;GuestAdapter adapter(disabled,memory,EstablishedGuestContext{});
        guest_error(adapter,a,memory,GuestCall::Socket,NetworkDown,{2,2,17});
        check(memory.get32(eb)==88,"errno isolation across guest TLS contexts");
        check(call(adapter,a,GuestCall::SetLastErrno,{0x12345678}) && a.x[0]==0x12345678,"SetLastErrno void preserves x0");
        check(call(adapter,a,GuestCall::GetLastErrno) && a.x[0]==0x12345678,"GetLastErrno little-endian TLS read");
    }
    {
        Udp udp(true);GuestAdapter adapter(udp,memory,EstablishedGuestContext{});
        memory.deny_write=ea;
        check(!call(adapter,a,GuestCall::Socket,{2,2,17}),"missing writable TLS is explicit adapter failure");
        memory.deny_write=0;
        std::array<int,Udp::kDescriptors> descriptors{};
        for(auto& fd:descriptors) { auto result=udp.socket(2,2,17);check(bool(result),"invalid TLS must not allocate a host descriptor");fd=int(result.value); }
        check(udp.socket(2,2,17).error==TooManyFiles,"socket count remains bounded");
        for(auto fd:descriptors)udp.close(fd);
        CPU invalid=context(std::numeric_limits<uint64_t>::max()-100);
        check(!call(adapter,invalid,GuestCall::Socket,{2,2,17}),"TLS offset overflow rejected");
    }
    Udp udp(true);GuestAdapter adapter(udp,memory,EstablishedGuestContext{});
    memory.put32(ea,77);memory.put32(eb,88);
    const int sender=socket(adapter,a),receiver=socket(adapter,b);
    check(memory.get32(ea)==77 && memory.get32(eb)==88,"success preserves independent guest errno");
    const auto source=bind(adapter,a,memory,udp,sender,0x3000);
    auto destination=bind(adapter,b,memory,udp,receiver,0x3100);
    destination[0]=std::byte{0};memory.put(0x3200,destination);
    const std::array<std::byte,8> data{std::byte{1},std::byte{2},std::byte{0},std::byte{4},std::byte{5},std::byte{6},std::byte{7},std::byte{8}};
    memory.put(0x4000,data);
    const auto send=[&] {
        check(call(adapter,a,GuestCall::SendTo,{uint64_t(sender),0x4000,data.size(),DontWait,0x3200,16}) && a.x[0]==data.size(),"guest SendTo datagram");
        wait(udp,receiver);
    };
    guest_error(adapter,b,memory,GuestCall::RecvFrom,WouldBlock,{uint64_t(receiver),0x5000,8,0,0,0});
    check(memory.get32(ea)==77,"would-block errno does not leak to sender guest thread");
    // Real scalar option translation/readback, with complete guest bounds.
    const auto set_option=[&](uint32_t level,uint32_t option,uint32_t value) {
        memory.put32(0x8000,value);
        check(call(adapter,a,GuestCall::SetSockOpt,{uint64_t(sender),level,option,0x8000,4}) && a.x[0]==0,"guest SetSockOpt actual OS option");
    };
    const auto get_option=[&](uint32_t level,uint32_t option) {
        memory.put32(0x8100,28);
        check(call(adapter,a,GuestCall::GetSockOpt,{uint64_t(sender),level,option,0x8200,0x8100}) && a.x[0]==0,"guest GetSockOpt actual OS query");
        check(memory.get32(0x8100)==4 && memory.bytes[0x8204-Memory::base]==std::byte{0xcc},"scalar option copy bounded to4bytes with actual length");
        return memory.get32(0x8200);
    };
    set_option(0xffff,0x20,1);check(get_option(0xffff,0x20)==1,"broadcast flag actually enabled");
    set_option(0xffff,0x20,0);check(get_option(0xffff,0x20)==0,"broadcast flag actually disabled");
    set_option(0xffff,4,1);check(get_option(0xffff,4)==1,"reuseaddr readback");
    for(uint32_t ttl:{1u,64u,255u}) { set_option(0,4,ttl);check(get_option(0,4)==ttl,"unicastTTL preserved by real OS"); }
    memory.put32(0x8000,0);
    guest_error(adapter,a,memory,GuestCall::SetSockOpt,Invalid,{uint64_t(sender),0,4,0x8000,4});
    memory.put32(0x8000,256);
    guest_error(adapter,a,memory,GuestCall::SetSockOpt,Invalid,{uint64_t(sender),0,4,0x8000,4});
    check(get_option(0,4)==255,"invalid TTL never changes host state");
    guest_error(adapter,a,memory,GuestCall::SetSockOpt,Invalid,{uint64_t(sender),0,4,0x8000,3});
    guest_error(adapter,a,memory,GuestCall::SetSockOpt,Fault,{uint64_t(sender),0,4,~uint64_t(1),4});
    guest_error(adapter,a,memory,GuestCall::SetSockOpt,ProtocolOption,{uint64_t(sender),6,1,0x8000,4});
    memory.put32(0x8100,3);memory.put32(0x8200,0x12345678);
    guest_error(adapter,a,memory,GuestCall::GetSockOpt,Invalid,{uint64_t(sender),0,4,0x8200,0x8100});
    check(memory.get32(0x8100)==3 && memory.get32(0x8200)==0x12345678,"short option capacity fails without partial output");
    memory.put32(0x8100,4);memory.deny_write=0x8100;
    guest_error(adapter,a,memory,GuestCall::GetSockOpt,Fault,{uint64_t(sender),0,4,0x8200,0x8100});
    memory.deny_write=0;
    guest_error(adapter,a,memory,GuestCall::GetSockOpt,Fault,{uint64_t(sender),0,4,~uint64_t(1),0x8100});
    guest_error(adapter,a,memory,GuestCall::GetSockOpt,Fault,{uint64_t(sender),0,4,0x8200,~uint64_t(1)});
    check(memory.get32(0x8100)==4 && memory.get32(0x8200)==0x12345678,"invalid output pointers leave option outputs intact");
#ifdef _WIN32
    memory.put32(0x8000,1);
    guest_error(adapter,a,memory,GuestCall::SetSockOpt,Unsupported,{uint64_t(sender),0xffff,0x200,0x8000,4});
    guest_error(adapter,a,memory,GuestCall::GetSockOpt,Unsupported,{uint64_t(sender),0xffff,0x200,0x8200,0x8100});
#else
    const auto native_reuse=udp.set_option(sender,Option::ReusePort,true);
    memory.put32(0x8000,1);
    if(native_reuse) {
        set_option(0xffff,0x200,1);check(get_option(0xffff,0x200)==1,"native reuseport real readback");
    } else guest_error(adapter,a,memory,GuestCall::SetSockOpt,native_reuse.error,{uint64_t(sender),0xffff,0x200,0x8000,4});
#endif
    // GetSockName truncates only the copy; required length stays16.
    memory.put32(0x8100,28);
    check(call(adapter,a,GuestCall::GetSockName,{uint64_t(sender),0x8300,0x8100}) && a.x[0]==0 && memory.get32(0x8100)==16 &&
          std::equal(source.begin(),source.end(),memory.bytes.begin()+0x8300-Memory::base),"guest Getsockname real bound IPv4 address");
    check(memory.bytes[0x8310-Memory::base]==std::byte{0xcc},"Getsockname does not overwrite capacity28 tail");
    memory.put32(0x8100,2);
    check(call(adapter,a,GuestCall::GetSockName,{uint64_t(sender),0x8400,0x8100}) && a.x[0]==0 && memory.get32(0x8100)==16 &&
          memory.bytes[0x8400-Memory::base]==std::byte{16} && memory.bytes[0x8401-Memory::base]==std::byte{2} &&
          memory.bytes[0x8402-Memory::base]==std::byte{0xcc},"Getsockname short capacity bounded copy");
    memory.put32(0x8100,0);
    check(call(adapter,a,GuestCall::GetSockName,{uint64_t(sender),0,0x8100}) && a.x[0]==0 && memory.get32(0x8100)==16,"Getsockname zero capacity permits NULL address");
    memory.put32(0x8100,28);
    guest_error(adapter,a,memory,GuestCall::GetSockName,Fault,{uint64_t(sender),~uint64_t(1),0x8100});
    guest_error(adapter,a,memory,GuestCall::GetSockName,Fault,{uint64_t(sender),0x8300,~uint64_t(1)});
    check(memory.get32(0x8100)==28,"Getsockname invalid output leaves input length unchanged");
    // Logical nonblocking is truthful; requests for blocking are not accepted.
    check(call(adapter,a,GuestCall::Fcntl,{uint64_t(sender),3}) && a.x[0]==0x802,"F_GETFL reflects validated always-nonblocking descriptor");
    for(uint32_t flags:{0x800u,0x802u})check(call(adapter,a,GuestCall::Fcntl,{uint64_t(sender),4,flags}) && a.x[0]==0,"F_SETFL accepts supported already-nonblocking flags");
    for(uint32_t flags:{0u,2u,0x1000u})guest_error(adapter,a,memory,GuestCall::Fcntl,Unsupported,{uint64_t(sender),4,flags});
    guest_error(adapter,a,memory,GuestCall::Fcntl,Unsupported,{uint64_t(sender),9,0});
    guest_error(adapter,a,memory,GuestCall::Fcntl,BadDescriptor,{negative(-1),3});
    guest_error(adapter,a,memory,GuestCall::Shutdown,Invalid,{uint64_t(sender),3});
    guest_error(adapter,a,memory,GuestCall::SendFailure,Unsupported,{uint64_t(sender),~uint64_t(0),8,0});
    check(memory.get32(eb)==WouldBlock,"new socket errors remain isolated to caller guest TLS");
    send();check(call(adapter,b,GuestCall::Recv,{uint64_t(receiver),0,0,0,~uint64_t(0),~uint64_t(0)}) && b.x[0]==0,"plain Recv zero capacity consumes no packet");
    check(call(adapter,b,GuestCall::Recv,{uint64_t(receiver),0x5000,8,0,~uint64_t(0),~uint64_t(0)}) && b.x[0]==8,
          "plain Recv ignores sender registers and receives queued payload");
    guest_error(adapter,b,memory,GuestCall::Recv,WouldBlock,{uint64_t(receiver),0x5000,8,0});
    guest_error(adapter,b,memory,GuestCall::Recv,Invalid,{uint64_t(receiver),0,8,0});
    guest_error(adapter,b,memory,GuestCall::Recv,Fault,{uint64_t(receiver),0x5000,uint64_t(1)<<32,0});
    guest_error(adapter,a,memory,GuestCall::SendTo,Unsupported,{uint64_t(sender),0x4000,8,0,0,0});
    guest_error(adapter,a,memory,GuestCall::SendTo,Invalid,{uint64_t(sender),0,8,0,0x3200,16});
    guest_error(adapter,a,memory,GuestCall::SendTo,Fault,{uint64_t(sender),0x4000,uint64_t(1)<<32,0,0x3200,16});
    guest_error(adapter,a,memory,GuestCall::SendTo,MessageSize,{uint64_t(sender),0x4000,Udp::kMaxDatagram+1,0,0x3200,16});
    guest_error(adapter,a,memory,GuestCall::SendTo,Fault,{uint64_t(sender),~uint64_t(3),8,0,0x3200,16});
    guest_error(adapter,a,memory,GuestCall::SendTo,Fault,{uint64_t(sender),0x4000,8,0,~uint64_t(3),16});
    memory.fail_read=0x4000;
    guest_error(adapter,a,memory,GuestCall::SendTo,Fault,{uint64_t(sender),0x4000,8,0,0x3200,16});
    memory.fail_read=0;
    guest_error(adapter,b,memory,GuestCall::RecvFrom,Invalid,{uint64_t(receiver),0,8,0,0,0});
    guest_error(adapter,b,memory,GuestCall::RecvFrom,Fault,{uint64_t(receiver),0x5000,uint64_t(1)<<32,0,0,0});
    guest_error(adapter,b,memory,GuestCall::RecvFrom,Invalid,{uint64_t(receiver),0,uint64_t(1)<<32,0,0,0});
    check(call(adapter,a,GuestCall::SendTo,{uint64_t(sender),0,0,0,0x3200,16}) && a.x[0]==0,"SDK zero SendTo returns0 without packet");
    PollFd empty{receiver,Readable};check(udp.poll({&empty,1},0).value==0,"no datagram from zero-size SDK send or rejected pointers");
    // Prevalidate every output before consuming an already queued datagram.
    send();memory.put32(0x6100,28);
    guest_error(adapter,b,memory,GuestCall::RecvFrom,Fault,{uint64_t(receiver),~uint64_t(3),8,0,0,0});
    guest_error(adapter,b,memory,GuestCall::RecvFrom,Fault,{uint64_t(receiver),0x5000,8,0,~uint64_t(3),0x6100});
    guest_error(adapter,b,memory,GuestCall::RecvFrom,Fault,{uint64_t(receiver),0x5000,8,0,0x6000,~uint64_t(3)});
    check(call(adapter,b,GuestCall::RecvFrom,{uint64_t(receiver),0,0,0,~uint64_t(0),~uint64_t(0)}) && b.x[0]==0,"SDK zero receive ignores pointers and consumes no packet");
    check(call(adapter,b,GuestCall::RecvFrom,{uint64_t(receiver),0x5000,8,0,0x6000,0x6100}) && b.x[0]==8,"packet survives rejected output mappings and zero receive");
    check(std::equal(data.begin(),data.end(),memory.bytes.begin()+0x5000-Memory::base),"actual guest receive bytes");
    check(memory.get32(0x6100)==16 && std::equal(source.begin(),source.end(),memory.bytes.begin()+0x6000-Memory::base),"source length28 becomes actual16 with BSD network endian address");
    check(memory.bytes[0x5008-Memory::base]==std::byte{0xcc} && memory.bytes[0x6010-Memory::base]==std::byte{0xcc},"output writes stay within data and actual address spans");
    memory.put32(0x6100,28);
    guest_error(adapter,b,memory,GuestCall::RecvFrom,WouldBlock,{uint64_t(receiver),0x5000,8,0,0x6000,0x6100});
    check(memory.get32(0x6100)==28,"receive failure preserves caller address length");
    // NULL optional sender outputs and a zero input socklen delegate to Recv.
    send();check(call(adapter,b,GuestCall::RecvFrom,{uint64_t(receiver),0x5000,8,0,0,~uint64_t(0)}) && b.x[0]==8,"NULL source ignores invalid length pointer");
    send();check(call(adapter,b,GuestCall::RecvFrom,{uint64_t(receiver),0x5000,8,0,~uint64_t(0),0}) && b.x[0]==8,"NULL length pointer ignores invalid source buffer");
    send();memory.put32(0x6100,0);memory.deny_write=0x6100;
    check(call(adapter,b,GuestCall::RecvFrom,{uint64_t(receiver),0x5000,8,0,~uint64_t(0),0x6100}) && b.x[0]==8,"zero input socklen ignores output buffer and needs length read only");
    memory.deny_write=0;check(memory.get32(0x6100)==0,"ignored zero source length unchanged");
    // Peek, truncated data and truncated sockaddr follow explicit bounded ABI.
    send();memory.put32(0x6100,2);
    check(call(adapter,b,GuestCall::RecvFrom,{uint64_t(receiver),0x5000,2,Peek,0x6200,0x6100}) && b.x[0]==2,"peek copies bounded prefix");
    check(memory.get32(0x6100)==16 && memory.bytes[0x6200-Memory::base]==std::byte{16} && memory.bytes[0x6202-Memory::base]==std::byte{0xcc},"short source capacity writes actual length without overflow");
    check(call(adapter,b,GuestCall::RecvFrom,{uint64_t(receiver),0x5000,2,0,0,0}) && b.x[0]==2,"nonpeek short receive discards remainder");
    guest_error(adapter,b,memory,GuestCall::RecvFrom,WouldBlock,{uint64_t(receiver),0x5000,8,0,0,0});
    // Poll decodes exactly 8 guest little-endian bytes per entry, preserves fd/events.
    memory.put32(0x7000,uint32_t(sender));memory.put32(0x7004,uint32_t(Writable)|0xbeef0000u);
    memory.put32(0x7008,uint32_t(-1));memory.put32(0x700c,uint32_t(Readable)|0xcafe0000u);
    check(call(adapter,a,GuestCall::Poll,{0x7000,2,0}) && a.x[0]==1,"guest Poll writable and negative descriptor ignore");
    check(memory.get32(0x7000)==uint32_t(sender) && memory.get32(0x7004)==uint32_t(Writable|(Writable<<16)) && memory.get32(0x700c)==Readable,"Poll writes only revents little-endian fields");
    guest_error(adapter,a,memory,GuestCall::Poll,Invalid,{0x7000,Udp::kDescriptors+1,0});
    guest_error(adapter,a,memory,GuestCall::Poll,Invalid,{0,0,negative(-1)});
    guest_error(adapter,a,memory,GuestCall::Poll,Invalid,{0,0,Udp::kMaxPollMs+1});
    guest_error(adapter,a,memory,GuestCall::Poll,Fault,{~uint64_t(3),1,0});
    check(call(adapter,a,GuestCall::Poll,{0,0,0}) && a.x[0]==0,"zero count permits NULL guest array");
    // Missing TLS prevents even Close from changing descriptor lifetime.
    memory.deny_write=ea;
    check(!call(adapter,a,GuestCall::Close,{uint64_t(sender)}),"missing TLS rejects Close before side effect");
    memory.deny_write=0;Address alive{};check(bool(udp.local_address(sender,alive)),"descriptor remains live after rejected Close");
    // An errno write unexpectedly failing after validation is also adapterfalse.
    memory.fail_write=ea;
    check(!call(adapter,a,GuestCall::Close,{~uint64_t(0)}),"errno copy failure cannot report completed guest error");
    memory.fail_write=0;
    // Unexpected post-validation copy failures cannot masquerade as completion.
    send();memory.fail_write=0x5000;
    check(!call(adapter,b,GuestCall::RecvFrom,{uint64_t(receiver),0x5000,8,0,0,0}),"output copy failure explicit false after packet consumed");
    memory.fail_write=0;std::array<std::byte,8> scratch{};
    check(udp.receive_from(receiver,scratch,0).error==WouldBlock,"false must not invite fallback: packet already consumed");
    {
        Udp cancelled(true);GuestAdapter adapter2(cancelled,memory,EstablishedGuestContext{});CPU c=context(0x2000);
        std::atomic<bool> started{false};bool handled=false;
        std::thread poller([&] { started=true;handled=call(adapter2,c,GuestCall::Poll,{0,0,1000}); });
        while(!started.load())std::this_thread::yield();
        const auto begin=std::chrono::steady_clock::now();cancelled.cancel();poller.join();
        check(handled && c.x[0]==~uint64_t(0) && memory.get32(eb)==Interrupted,"Poll cancellation becomes per-thread guest EINTR");
        check(std::chrono::steady_clock::now()-begin<std::chrono::seconds(2),"bounded cancel response");
    }
    check(call(adapter,a,GuestCall::Close,{uint64_t(sender)}) && a.x[0]==0,"guest Close sender");
    check(call(adapter,b,GuestCall::Close,{uint64_t(receiver)}) && b.x[0]==0,"guest Close receiver");
    guest_error(adapter,a,memory,GuestCall::Close,BadDescriptor,{uint64_t(sender)});
    std::puts("Native guest adapter: validated fake memory, TLS isolation, ABI preservation, bounds, raw sockaddr0, SDK zero/optional outputs, poll/cancel and loopback passed (uninstalled)");
}
