#include "native_lan_dispatch.h"
#include <array>
#include <charconv>
#include <limits>
#include <string>

namespace nx::native_lan {
namespace {
using Op=profile::Operation;
using Ret=profile::ReturnKind;
constexpr uint32_t kUnavailable=(3400u<<9)|110u;
constexpr uint32_t kBackendFailure=(1u<<9)|110u;
void result(CPU& cpu,Ret kind,uint32_t value) {
    if(kind==Ret::Void)return;
    cpu.x[0]=kind==Ret::Result22?value&0x3fffffu:value;
}
std::array<std::byte,4> word(uint32_t value,bool network=false) {
    std::array<std::byte,4> bytes{};
    for(unsigned i=0;i<4;++i)bytes[i]=std::byte((value>>(network?24-i*8:i*8))&255);
    return bytes;
}
bool valid(GuestMemory& memory,uint64_t address,size_t size,MemoryAccess access) {
    return !size || (address && size<=UINT64_MAX-address && memory.validate(address,size,access));
}
bool errno_cell(GuestMemory& memory,const CPU& cpu,uint64_t& address) {
    if(cpu.tpidrro>UINT64_MAX-GuestAdapter::kErrnoOffset)return false;
    address=cpu.tpidrro+GuestAdapter::kErrnoOffset;
    return valid(memory,address,4,MemoryAccess::ReadWrite);
}
bool failure(Backend& backend,GuestMemory& memory,CPU& cpu,int error,bool pointer=false) {
    if(backend.socket_initialized()) {
        uint64_t cell;
        if(!errno_cell(memory,cpu,cell) || !memory.write(cell,word(uint32_t(error))))return false;
    }
    cpu.x[0]=pointer?0:uint64_t(-1);
    return true;
}
bool string_at(GuestMemory& memory,uint64_t address,std::string& text) {
    for(size_t i=0;i<16;++i) {
        if(address>UINT64_MAX-i || !valid(memory,address+i,1,MemoryAccess::Read))return false;
        std::byte byte;
        if(!memory.read(address+i,{&byte,1}))return false;
        if(byte==std::byte{0})return true;
        text.push_back(char(std::to_integer<unsigned>(byte)));
    }
    return true; // A 16-character non-address is classified as invalid text.
}
bool inet_call(Backend& backend,GuestMemory& memory,CPU& cpu,Op op) {
    if(uint32_t(cpu.x[0])!=2)return failure(backend,memory,cpu,AddressFamily,op==Op::InetNtop);
    if(op==Op::InetPton) {
        std::string text;
        if(!string_at(memory,cpu.x[1],text))return failure(backend,memory,cpu,Fault);
        uint32_t address;
        if(!parse_local_ipv4(text,address)) {cpu.x[0]=0;return true;}
        if(!valid(memory,cpu.x[2],4,MemoryAccess::Write))return failure(backend,memory,cpu,Fault);
        if(!memory.write(cpu.x[2],word(address,true)))return false;
        cpu.x[0]=1;return true;
    }
    if(!valid(memory,cpu.x[1],4,MemoryAccess::Read))return failure(backend,memory,cpu,Fault,true);
    std::array<std::byte,4> bytes{};
    if(!memory.read(cpu.x[1],bytes))return false;
    std::string text;
    for(auto byte:bytes) {
        if(!text.empty())text+='.';
        text+=std::to_string(std::to_integer<unsigned>(byte));
    }
    text+='\0';
    if(uint32_t(cpu.x[3])<text.size())return failure(backend,memory,cpu,28,true); // ENOSPC
    if(!valid(memory,cpu.x[2],text.size(),MemoryAccess::Write))return failure(backend,memory,cpu,Fault,true);
    if(!memory.write(cpu.x[2],{reinterpret_cast<const std::byte*>(text.data()),text.size()}))return false;
    cpu.x[0]=cpu.x[2];return true;
}
}
bool parse_local_ipv4(std::string_view text,uint32_t& address) noexcept {
    uint32_t parsed=0;
    for(unsigned octet=0;octet<4;++octet) {
        const auto end=text.find('.');
        const auto part=text.substr(0,end);
        if(part.empty() || part.size()>3 || (part.size()>1&&part.front()=='0'))return false;
        unsigned value=0;
        const auto conversion=std::from_chars(part.data(),part.data()+part.size(),value);
        if(conversion.ec!=std::errc{} || conversion.ptr!=part.data()+part.size() || value>255)return false;
        parsed=(parsed<<8)|value;
        if(octet<3) {if(end==text.npos)return false;text.remove_prefix(end+1);}
        else if(end!=text.npos)return false;
    }
    address=parsed;return true;
}
bool dispatch(Backend& backend,GuestMemory& memory,CPU& cpu,Op operation,Ret kind) {
    switch(operation) {
    case Op::Initialize:
    case Op::InitializeConfig: {
        // The replacement owns host resources, not the SDK's IPC pool/client.
        // A supplied configuration must at least be a readable guest object;
        // no guest allocation is retained or converted into an SDK client.
        uint64_t cell;
        if(!errno_cell(memory,cpu,cell) ||
           (operation==Op::InitializeConfig && !valid(memory,cpu.x[0],1,MemoryAccess::Read)) ||
           (operation==Op::Initialize && cpu.x[1] &&
            (cpu.x[1]>(1ull<<30) || !valid(memory,cpu.x[0],size_t(cpu.x[1]),MemoryAccess::ReadWrite)))) {
            result(cpu,kind,kBackendFailure);return true;
        }
        const auto initialized=backend.initialize_sockets();
        result(cpu,kind,initialized?0:kBackendFailure);return true;
    }
    case Op::Finalize: backend.finalize_sockets();result(cpu,kind,0);return true;
    case Op::IsInitialized: cpu.x[0]=backend.socket_initialized();return true;
    case Op::NifmInitialize: result(cpu,kind,backend.initialize_nifm()==NifmStatus::Ok?0:kUnavailable);return true;
    case Op::NifmSetLocalMode: backend.set_local_mode(uint8_t(cpu.x[0])!=0);return true;
    case Op::NifmSubmit:
    case Op::NifmSubmitAndWait: backend.submit_request();return true;
    case Op::NifmCancel: backend.cancel_request();return true;
    case Op::NifmAvailable: cpu.x[0]=backend.network_available();return true;
    case Op::NifmOnHold: cpu.x[0]=0;return true; // Requests finish synchronously in this provider.
    case Op::NifmRequestError: result(cpu,kind,backend.network_available()?0:kUnavailable);return true;
    case Op::NifmProxyUnavailable: result(cpu,kind,kUnavailable);return true;
    case Op::NifmIpConfig: {
        backend.refresh(); // Enumerate before acquiring any guest-output pins.
        uint32_t fields=0;
        for(unsigned i=0;i<5;++i) {
            if(cpu.x[i]) {
                fields|=1u<<i;
                if(!valid(memory,cpu.x[i],4,MemoryAccess::Write)) {result(cpu,kind,kUnavailable);return true;}
            }
        }
        const auto configuration=backend.config(fields);
        if(configuration.status!=NifmStatus::Ok) {result(cpu,kind,kUnavailable);return true;}
        const auto& c=configuration.config;
        const uint32_t values[]={c.address,c.mask,c.gateway,c.dns_primary,c.dns_secondary};
        for(unsigned i=0;i<5;++i)if(cpu.x[i]&&!memory.write(cpu.x[i],word(values[i],true)))return false;
        result(cpu,kind,0);return true;
    }
    case Op::GetErrno:
    case Op::SetErrno: {
        // One admission snapshot makes finalize linearizable: a setter can
        // never fall through to an integer failure that clobbers its void ABI.
        const auto admitted=backend.acquire(BackendAccess::Cleanup);
        if(!admitted) {
            if(operation==Op::GetErrno)cpu.x[0]=Invalid;
            return true;
        }
        GuestAdapter adapter(*admitted.transport,memory,EstablishedGuestContext{});
        return adapter.invoke(cpu,operation==Op::GetErrno?GuestCall::GetLastErrno:GuestCall::SetLastErrno);
    }
    case Op::ResolveFailure: cpu.x[0]=4;return true; // EAI_FAIL; no DNS request or result allocation.
    case Op::HostResolveFailure: return failure(backend,memory,cpu,Unsupported,true);
    case Op::ResolverGetOption:
    case Op::ResolverSetOption:
    case Op::UnsupportedSocket: return failure(backend,memory,cpu,Unsupported);
    case Op::InetPton:
    case Op::InetNtop: return inet_call(backend,memory,cpu,operation);
    case Op::PreserveOriginal:
    case Op::Count: return false; // Planner must never install a dispatcher for a preserved entry.
    default: break;
    }
    const bool cleanup=operation==Op::Close || operation==Op::GetErrno || operation==Op::SetErrno ||
        operation==Op::GetSockOpt || operation==Op::GetSockName || operation==Op::Shutdown || operation==Op::Fcntl;
    const auto access=backend.acquire(cleanup?BackendAccess::Cleanup:BackendAccess::Network);
    if(!access)return failure(backend,memory,cpu,access.error?access.error:NetworkDown);
    GuestAdapter adapter(*access.transport,memory,EstablishedGuestContext{});
    GuestCall call;
    switch(operation) {
    case Op::Socket: call=GuestCall::Socket;break;
    case Op::Bind: call=GuestCall::Bind;break;
    case Op::SendTo: call=GuestCall::SendTo;break;
    case Op::RecvFrom: call=GuestCall::RecvFrom;break;
    case Op::Poll: call=GuestCall::Poll;break;
    case Op::Close: call=GuestCall::Close;break;
    case Op::GetErrno: call=GuestCall::GetLastErrno;break;
    case Op::SetErrno: call=GuestCall::SetLastErrno;break;
    case Op::SetSockOpt: call=GuestCall::SetSockOpt;break;
    case Op::GetSockOpt: call=GuestCall::GetSockOpt;break;
    case Op::Shutdown: call=GuestCall::Shutdown;break;
    case Op::GetSockName: call=GuestCall::GetSockName;break;
    case Op::Recv: call=GuestCall::Recv;break;
    case Op::Read: {
        CPU copy=cpu;copy.x[3]=0;
        if(!adapter.invoke(copy,GuestCall::Recv))return false;
        cpu.x[0]=copy.x[0];return true;
    }
    case Op::Fcntl: call=GuestCall::Fcntl;break;
    case Op::Send:
    case Op::Write: call=GuestCall::SendFailure;break;
    default: return false;
    }
    return adapter.invoke(cpu,call);
}
}
