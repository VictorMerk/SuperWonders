#include "native_lan_guest.h"
#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <new>
#include <vector>

namespace nx::native_lan {
namespace {
int32_t i32(uint64_t value) { return std::bit_cast<int32_t>(uint32_t(value)); }
uint32_t load32(std::span<const std::byte> p) {
    uint32_t value=0;
    for(unsigned i=0;i<4;++i)value|=uint32_t(std::to_integer<unsigned>(p[i]))<<(i*8);
    return value;
}
std::array<std::byte,4> store32(uint32_t value) {
    std::array<std::byte,4> bytes{};
    for(unsigned i=0;i<4;++i)bytes[i]=std::byte((value>>(i*8))&255);
    return bytes;
}
void normalize_address(Address& address) {
    // Raw Wonder IPv4 sockaddr builders leave sin_len0; typed builders use16.
    // Normalize only our private copy, never the guest's descriptor bytes.
    if(address[0]==std::byte{0} && address[1]==std::byte{2})address[0]=std::byte{16};
}
int16_t load16(std::span<const std::byte> p) {
    return std::bit_cast<int16_t>(uint16_t(std::to_integer<unsigned>(p[0])|(std::to_integer<unsigned>(p[1])<<8)));
}
bool boolean_option(uint32_t level,uint32_t option,Option& result) {
    if(level!=0xffff)return false;
    switch(option) {
    case 4: result=Option::ReuseAddress;return true;
    case 0x20: result=Option::Broadcast;return true;
    case 0x200: result=Option::ReusePort;return true;
    default:return false;
    }
}
}
bool GuestAdapter::valid(uint64_t address,size_t size,MemoryAccess access) noexcept {
    if(!size)return true; // NULL is allowed only for a zero-length span.
    if(!address || size>std::numeric_limits<uint64_t>::max()-address)return false;
    return memory_.validate(address,size,access);
}
bool GuestAdapter::finish(CPU& cpu,uint64_t tls,const Result& result) noexcept {
    if(result.error && !memory_.write(tls,store32(uint32_t(result.error))))return false;
    cpu.x[0]=uint64_t(result.error?-1:result.value);
    return true;
}
bool GuestAdapter::invoke(CPU& cpu,GuestCall call) {
    if(cpu.tpidrro>std::numeric_limits<uint64_t>::max()-kErrnoOffset)return false;
    const uint64_t tls=cpu.tpidrro+kErrnoOffset;
    if(!valid(tls,4,MemoryAccess::ReadWrite))return false;
    const auto fail=[&](int error) { return finish(cpu,tls,{-1,error,false}); };
    try {
        switch(call) {
        case GuestCall::GetLastErrno: {
            std::array<std::byte,4> bytes{};
            if(!memory_.read(tls,bytes))return false;
            cpu.x[0]=load32(bytes); return true;
        }
        case GuestCall::SetLastErrno:
            return memory_.write(tls,store32(uint32_t(cpu.x[0])));
        case GuestCall::Socket:
            return finish(cpu,tls,udp_.socket(i32(cpu.x[0]),i32(cpu.x[1]),i32(cpu.x[2])));
        case GuestCall::Close:
            return finish(cpu,tls,udp_.close(i32(cpu.x[0])));
        case GuestCall::Shutdown:
            return finish(cpu,tls,udp_.shutdown(i32(cpu.x[0]),i32(cpu.x[1])));
        case GuestCall::SendFailure:
            return fail(Unsupported); // No connected socket or payload side effect.
        case GuestCall::Fcntl: {
            Address ignored{};
            const auto descriptor=udp_.local_address(i32(cpu.x[0]),ignored);
            if(!descriptor)return finish(cpu,tls,descriptor);
            const int32_t command=i32(cpu.x[1]);
            if(command==3)return finish(cpu,tls,{0x802,0,false});
            if(command!=4 || (uint32_t(cpu.x[2])!=0x800 && uint32_t(cpu.x[2])!=0x802))return fail(Unsupported);
            return finish(cpu,tls,{0,0,false}); // Actual host socket already nonblocking.
        }
        case GuestCall::SetSockOpt:
        case GuestCall::GetSockOpt: {
            const bool ttl=uint32_t(cpu.x[1])==0 && uint32_t(cpu.x[2])==4;
            Option option{};
            if(!ttl && !boolean_option(uint32_t(cpu.x[1]),uint32_t(cpu.x[2]),option))return fail(ProtocolOption);
            if(call==GuestCall::SetSockOpt) {
                if(uint32_t(cpu.x[4])!=4)return fail(Invalid);
                if(!valid(cpu.x[3],4,MemoryAccess::Read))return fail(Fault);
                std::array<std::byte,4> bytes{};
                if(!memory_.read(cpu.x[3],bytes))return fail(Fault);
                const int32_t value=i32(load32(bytes));
                return finish(cpu,tls,ttl?udp_.set_unicast_ttl(i32(cpu.x[0]),value):
                    udp_.set_option(i32(cpu.x[0]),option,value!=0));
            }
            if(!valid(cpu.x[4],4,MemoryAccess::ReadWrite))return fail(Fault);
            std::array<std::byte,4> length{};
            if(!memory_.read(cpu.x[4],length))return fail(Fault);
            if(load32(length)<4)return fail(Invalid);
            if(!valid(cpu.x[3],4,MemoryAccess::Write))return fail(Fault);
            const auto result=ttl?udp_.get_unicast_ttl(i32(cpu.x[0])):udp_.get_option(i32(cpu.x[0]),option);
            if(!result)return finish(cpu,tls,result);
            if(!memory_.write(cpu.x[3],store32(uint32_t(result.value))) || !memory_.write(cpu.x[4],store32(4)))return false;
            return finish(cpu,tls,{0,0,false});
        }
        case GuestCall::GetSockName: {
            if(!valid(cpu.x[2],4,MemoryAccess::ReadWrite))return fail(Fault);
            std::array<std::byte,4> length{};
            if(!memory_.read(cpu.x[2],length))return fail(Fault);
            const auto capacity=std::min(load32(length),uint32_t(16));
            if(!valid(cpu.x[1],capacity,MemoryAccess::Write))return fail(Fault);
            Address address{};
            const auto result=udp_.local_address(i32(cpu.x[0]),address);
            if(!result)return finish(cpu,tls,result);
            if(capacity && !memory_.write(cpu.x[1],std::span(address).first(capacity)))return false;
            if(!memory_.write(cpu.x[2],store32(16)))return false;
            return finish(cpu,tls,{0,0,false});
        }
        case GuestCall::Bind: {
            const uint32_t length=uint32_t(cpu.x[2]);
            if(length!=16)return fail(Invalid);
            if(!valid(cpu.x[1],length,MemoryAccess::Read))return fail(Fault);
            Address address{}; if(!memory_.read(cpu.x[1],address))return fail(Fault);
            normalize_address(address);
            return finish(cpu,tls,udp_.bind(i32(cpu.x[0]),address));
        }
        case GuestCall::SendTo: {
            // SDK delegates missing destination to connected Send; this adapter
            // deliberately exposes no connected-UDP operation.
            if(!cpu.x[4] || !uint32_t(cpu.x[5]))return fail(Unsupported);
            if(!cpu.x[2])return finish(cpu,tls,{0,0,false});
            if(!cpu.x[1])return fail(Invalid);
            if(cpu.x[2]>std::numeric_limits<uint32_t>::max())return fail(Fault);
            if(cpu.x[2]>Udp::kMaxDatagram)return fail(MessageSize);
            if(uint32_t(cpu.x[5])!=16)return fail(Invalid);
            const auto length=size_t(cpu.x[2]);
            if(!valid(cpu.x[1],length,MemoryAccess::Read) || !valid(cpu.x[4],16,MemoryAccess::Read))return fail(Fault);
            Address address{}; if(!memory_.read(cpu.x[4],address))return fail(Fault);
            normalize_address(address);
            std::vector<std::byte> bytes(length);
            if(length && !memory_.read(cpu.x[1],bytes))return fail(Fault);
            return finish(cpu,tls,udp_.send_to(i32(cpu.x[0]),bytes,i32(cpu.x[3]),address));
        }
        case GuestCall::Recv:
        case GuestCall::RecvFrom: {
            if(!cpu.x[2])return finish(cpu,tls,{0,0,false}); // SDK does not consume empty receive
            if(!cpu.x[1])return fail(Invalid);
            if(cpu.x[2]>std::numeric_limits<uint32_t>::max())return fail(Fault);
            if(cpu.x[2]>Udp::kMaxDatagram)return fail(MessageSize);
            const auto length=size_t(cpu.x[2]);
            if(!valid(cpu.x[1],length,MemoryAccess::Write))return fail(Fault);
            uint32_t address_capacity=0;
            if(call==GuestCall::RecvFrom && cpu.x[4] && cpu.x[5]) {
                if(!valid(cpu.x[5],4,MemoryAccess::Read))return fail(Fault);
                std::array<std::byte,4> bytes{};
                if(!memory_.read(cpu.x[5],bytes))return fail(Fault);
                address_capacity=std::min(load32(bytes),uint32_t(16));
                if(address_capacity && (!valid(cpu.x[4],address_capacity,MemoryAccess::Write) ||
                    !valid(cpu.x[5],4,MemoryAccess::Write)))return fail(Fault);
            }
            std::vector<std::byte> bytes(length); Address sender{};
            const auto result=udp_.receive_from(i32(cpu.x[0]),bytes,i32(cpu.x[3]),address_capacity?&sender:nullptr);
            if(!result)return finish(cpu,tls,result);
            if(result.value<0 || uint64_t(result.value)>length)return false;
            if(result.value && !memory_.write(cpu.x[1],std::span(bytes).first(size_t(result.value))))return false;
            if(address_capacity) {
                if(!memory_.write(cpu.x[4],std::span(sender).first(address_capacity)))return false;
                if(!memory_.write(cpu.x[5],store32(16)))return false;
            }
            return finish(cpu,tls,result);
        }
        case GuestCall::Poll: {
            if(cpu.x[1]>Udp::kDescriptors || i32(cpu.x[2])<0 || i32(cpu.x[2])>Udp::kMaxPollMs)return fail(Invalid);
            const auto count=size_t(cpu.x[1]), length=count*8;
            if(!valid(cpu.x[0],length,MemoryAccess::ReadWrite))return fail(Fault);
            std::array<std::byte,Udp::kDescriptors*8> raw{};
            if(length && !memory_.read(cpu.x[0],std::span(raw).first(length)))return fail(Fault);
            std::array<PollFd,Udp::kDescriptors> entries{};
            for(size_t i=0;i<count;++i) {
                const auto bytes=std::span(raw).subspan(i*8,8);
                entries[i]={i32(load32(bytes)),load16(bytes.subspan(4)),0};
            }
            const auto result=udp_.poll(std::span(entries).first(count),i32(cpu.x[2]));
            if(!result)return finish(cpu,tls,result);
            for(size_t i=0;i<count;++i) {
                const auto revents=uint16_t(entries[i].revents);
                const std::array<std::byte,2> bytes{std::byte(revents&255),std::byte(revents>>8)};
                if(!memory_.write(cpu.x[0]+i*8+6,bytes))return false;
            }
            return finish(cpu,tls,result);
        }
        }
    } catch(const std::bad_alloc&) { return fail(NoBuffers); }
    return false; // No fabricated success for an unsupported adapter operation.
}
}
