// Native LAN protocol. Controller packets authenticate their role, tick, session
// and payload. Networking alone does not establish deterministic game simulation.
#include "netplay_wire.h"
#include "launcher/update_crypto.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <bit>
#include <cmath>

namespace nx::lan {
namespace {
uint64_t get(const uint8_t* p,size_t n) {
    uint64_t v=0; for(size_t i=0;i<n;++i) v|=uint64_t(p[i])<<(8*i); return v;
}
void put(uint8_t* p,uint64_t v,size_t n) {
    for(size_t i=0;i<n;++i) { p[i]=uint8_t(v); v>>=8; }
}
Key hmac(std::span<const uint8_t> data,const Key& key) {
    std::array<uint8_t,64> inner{},outer{};
    for(size_t i=0;i<64;++i) {
        const uint8_t b=i<key.size()?key[i]:0;
        inner[i]=b^0x36; outer[i]=b^0x5c;
    }
    UcSha256 c; Key digest{};
    uc_sha256_init(&c); uc_sha256_update(&c,inner.data(),inner.size());
    uc_sha256_update(&c,data.data(),data.size()); uc_sha256_final(&c,digest.data());
    uc_sha256_init(&c); uc_sha256_update(&c,outer.data(),outer.size());
    uc_sha256_update(&c,digest.data(),digest.size()); uc_sha256_final(&c,digest.data());
    return digest;
}
bool valid_pad(const plat::PadState& p) {
    auto axis=[](int32_t n) { return n>=-plat::kStickMax && n<=plat::kStickMax; };
    return !(p.buttons&~((uint64_t(1)<<28)-1)) && axis(p.lx)&&axis(p.ly)&&axis(p.rx)&&axis(p.ry);
}
void put_pad(uint8_t* out,const plat::PadState& p) {
    if(!valid_pad(p)) throw std::runtime_error("Invalid LAN controller state");
    put(out,p.buttons,8);
    const int32_t axes[]={p.lx,p.ly,p.rx,p.ry};
    for(size_t i=0;i<4;++i) put(out+8+4*i,uint32_t(axes[i]),4);
}
plat::PadState get_pad(const uint8_t* p) {
    plat::PadState out{get(p,8),int32_t(get(p+8,4)),int32_t(get(p+12,4)),int32_t(get(p+16,4)),int32_t(get(p+20,4))};
    if(!valid_pad(out)) throw std::runtime_error("Invalid LAN controller state");
    return out;
}
void put_state(uint8_t* out,const PlayerState& state) {
    put(out,state.scene,8); put(out+8,state.valid?1:0,8);
    for(size_t i=0;i<3;++i) {
        const float value=state.position[i];
        if(!std::isfinite(value) || std::abs(value)>1e6f || (!state.valid && value!=0))
            throw std::runtime_error("Invalid LAN player-state position");
        put(out+16+4*i,std::bit_cast<uint32_t>(value),4);
    }
    put(out+28,0,4);
}
PlayerState get_state(const uint8_t* in) {
    if(get(in+8,8)>1 || get(in+28,4)) throw std::runtime_error("Invalid LAN player-state flags");
    PlayerState state; state.scene=get(in,8); state.valid=get(in+8,8)!=0;
    for(size_t i=0;i<3;++i) {
        state.position[i]=std::bit_cast<float>(uint32_t(get(in+16+4*i,4)));
        if(!std::isfinite(state.position[i]) || std::abs(state.position[i])>1e6f || (!state.valid && state.position[i]!=0))
            throw std::runtime_error("Invalid LAN player-state position");
    }
    return state;
}
}
bool compatible_state(const PlayerState& a,const PlayerState& b) {
    if(!a.valid || !b.valid) return true;
    if(a.scene!=b.scene) return false;
    for(size_t i=0;i<3;++i)
        if(!std::isfinite(a.position[i]) || !std::isfinite(b.position[i]) || std::abs(a.position[i]-b.position[i])>1.0f/1024.0f) return false;
    return true;
}
Key pairing_key(std::string_view code) {
    if(code.size()<4 || code.size()>64) throw std::runtime_error("Invalid LAN pairing code length");
    constexpr char context[]="SuperWonders native LAN protocol 4";
    UcSha256 c; Key key{}; uc_sha256_init(&c);
    uc_sha256_update(&c,context,sizeof context);
    uc_sha256_update(&c,code.data(),code.size()); uc_sha256_final(&c,key.data()); return key;
}
size_t packet_payload_size(std::span<const uint8_t> header) {
    if(header.size()!=kHeaderBytes || std::memcmp(header.data(),"SWLN",4) || get(header.data()+4,2)!=kProtocol || get(header.data()+10,2))
        throw std::runtime_error("Incompatible LAN packet header");
    const auto type=get(header.data()+6,2), sender=get(header.data()+8,2), size=get(header.data()+36,4);
    if(type<uint16_t(Message::Hello)||type>uint16_t(Message::SaveAck)||size>kMaxPayload ||
       (sender>=kPlayers && !(type==uint16_t(Message::Hello)&&sender==0xffff)))
        throw std::runtime_error("Invalid LAN packet bounds");
    return size_t(size);
}
std::vector<uint8_t> encode_packet(const Packet& p,const Key& key) {
    if(p.payload.size()>kMaxPayload) throw std::runtime_error("LAN packet too large");
    std::vector<uint8_t> bytes(kHeaderBytes+p.payload.size()+kMacBytes,0);
    std::memcpy(bytes.data(),"SWLN",4); put(bytes.data()+4,kProtocol,2);
    put(bytes.data()+6,uint16_t(p.type),2); put(bytes.data()+8,p.sender,2); put(bytes.data()+12,p.tick,8);
    std::memcpy(bytes.data()+20,p.session.data(),p.session.size()); put(bytes.data()+36,p.payload.size(),4);
    packet_payload_size(std::span(bytes.data(),kHeaderBytes));
    std::copy(p.payload.begin(),p.payload.end(),bytes.begin()+kHeaderBytes);
    const auto mac=hmac(std::span(bytes.data(),bytes.size()-kMacBytes),key);
    std::copy(mac.begin(),mac.end(),bytes.end()-kMacBytes); return bytes;
}
Packet decode_packet(std::span<const uint8_t> bytes,const Key& key) {
    if(bytes.size()<kHeaderBytes+kMacBytes) throw std::runtime_error("Truncated LAN packet");
    const size_t size=packet_payload_size(bytes.first(kHeaderBytes));
    if(bytes.size()!=kHeaderBytes+size+kMacBytes) throw std::runtime_error("Invalid LAN packet length");
    const auto mac=hmac(bytes.first(bytes.size()-kMacBytes),key);
    unsigned difference=0;
    for(size_t i=0;i<kMacBytes;++i) difference|=mac[i]^bytes[bytes.size()-kMacBytes+i];
    if(difference) throw std::runtime_error("LAN authentication failed");
    Packet p; p.type=Message(get(bytes.data()+6,2)); p.sender=uint16_t(get(bytes.data()+8,2)); p.tick=get(bytes.data()+12,8);
    std::copy_n(bytes.data()+20,p.session.size(),p.session.begin());
    p.payload.assign(bytes.begin()+kHeaderBytes,bytes.begin()+kHeaderBytes+size); return p;
}
std::vector<uint8_t> encode_input(const plat::PadState& pad,uint64_t hash,const PlayerState& state,uint64_t work,uint64_t event) {
    if (work>kMaxFrameWorkNs) throw std::runtime_error("Invalid LAN frame-work duration");
    std::vector<uint8_t> bytes(80); put_pad(bytes.data(),pad); put(bytes.data()+24,hash,8); put_state(bytes.data()+32,state);
    put(bytes.data()+64,work,8); put(bytes.data()+72,event,8); return bytes;
}
plat::PadState decode_input(std::span<const uint8_t> bytes,uint64_t& hash,PlayerState* state,uint64_t* work,uint64_t* event) {
    if(bytes.size()!=80) throw std::runtime_error("Invalid LAN input length");
    const uint64_t duration=get(bytes.data()+64,8);
    if (duration>kMaxFrameWorkNs) throw std::runtime_error("Invalid LAN frame-work duration");
    if (work) *work=duration;
    if (event) *event=get(bytes.data()+72,8);
    const auto decoded=get_state(bytes.data()+32); if(state) *state=decoded;
    hash=get(bytes.data()+24,8); return get_pad(bytes.data());
}
std::vector<uint8_t> encode_commit(const FrameInputs& frame) {
    if(frame.players<2||frame.players>kPlayers) throw std::runtime_error("Invalid LAN player count");
    std::vector<uint8_t> bytes(48+40*kPlayers,0); bytes[0]=frame.players; put(bytes.data()+8,frame.state_hash,8);
    put_state(bytes.data()+16+24*kPlayers,frame.state);
    for(size_t i=0;i<kPlayers;++i) {
        if (frame.frame_work_ns[i]>kMaxFrameWorkNs || (i>=frame.players && frame.frame_work_ns[i]))
            throw std::runtime_error("Invalid LAN peer timing");
        if(i>=frame.players && frame.loading_events[i]) throw std::runtime_error("Loading event for disconnected LAN player");
        put(bytes.data()+48+32*kPlayers+8*i,frame.loading_events[i],8);
        put_pad(bytes.data()+16+24*i,frame.pads[i]);
        put(bytes.data()+48+24*kPlayers+8*i,frame.frame_work_ns[i],8);
    }
    return bytes;
}
FrameInputs decode_commit(std::span<const uint8_t> bytes) {
    if(bytes.size()!=48+40*kPlayers || bytes[0]<2 || bytes[0]>kPlayers)
        throw std::runtime_error("Invalid LAN frame length");
    for(size_t i=1;i<8;++i) if(bytes[i]) throw std::runtime_error("Invalid LAN frame flags");
    FrameInputs frame; frame.players=bytes[0]; frame.state_hash=get(bytes.data()+8,8);
    frame.state=get_state(bytes.data()+16+24*kPlayers);
    for(size_t i=0;i<kPlayers;++i) {
        frame.loading_events[i]=get(bytes.data()+48+32*kPlayers+8*i,8);
        if(i>=frame.players && frame.loading_events[i]) throw std::runtime_error("Loading event for disconnected LAN player");
        frame.pads[i]=get_pad(bytes.data()+16+24*i);
        frame.frame_work_ns[i]=get(bytes.data()+48+24*kPlayers+8*i,8);
        if (frame.frame_work_ns[i]>kMaxFrameWorkNs || (i>=frame.players && frame.frame_work_ns[i]))
            throw std::runtime_error("Invalid LAN peer timing");
        const auto& p=frame.pads[i];
        if(i>=frame.players && (p.buttons||p.lx||p.ly||p.rx||p.ry)) throw std::runtime_error("Input for a disconnected LAN player");
    }
    return frame;
}
bool private_ipv4(uint32_t address) {
    return (address>>24)==127 || (address>>24)==10 || (address>>20)==0xac1 ||
           (address>>16)==0xc0a8 || (address>>16)==0xa9fe || (address>>22)==0x191;
}
}
