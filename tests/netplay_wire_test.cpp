#include "netplay_wire.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
static void check(bool ok,const char* message) {
    if(!ok) { std::fprintf(stderr,"%s\n",message); std::exit(1); }
}
static bool rejects(const std::function<void()>& fn) { try { fn(); return false; } catch(...) { return true; } }
int main() {
    using namespace nx::lan;
    const auto key=pairing_key("unit-test-pairing-code");
    FrameInputs input; input.players=4; input.tick=123; input.state_hash=0x123456789abcdef;
    input.state={42,true,{19.5f,91.0f,0.0f}};
    for(size_t i=0;i<4;++i) { input.pads[i].buttons=1ull<<i; input.pads[i].lx=int(i)*100; input.pads[i].ly=-int(i)*200; }
    Packet packet; packet.type=Message::Commit; packet.tick=input.tick; packet.session[0]=42; packet.payload=encode_commit(input);
    const auto encoded=encode_packet(packet,key); const auto decoded=decode_packet(encoded,key);
    const auto frame=decode_commit(decoded.payload);
    check(decoded.tick==123 && decoded.session==packet.session && frame.players==4 && frame.state_hash==input.state_hash,"authenticated frame roundtrip");
    check(frame.state.scene==42 && frame.state.valid && frame.state.position==input.state.position,"player-state bits survive authenticated encoding");
    auto small=frame.state; small.position[0]+=0.0001f;
    check(compatible_state(frame.state,small),"subpixel float variation is bounded explicitly");
    small.position[0]+=0.01f;
    check(!compatible_state(frame.state,small),"meaningful position differences fail the partial desync guard");
    small=frame.state; small.scene^=1;
    check(!compatible_state(frame.state,small),"different course states are rejected");
    auto invalid_state=input; invalid_state.state.position[0]=std::numeric_limits<float>::quiet_NaN();
    check(rejects([&]{encode_commit(invalid_state);}),"NaN player positions are rejected");
    for(size_t i=0;i<4;++i) check(frame.pads[i].buttons==input.pads[i].buttons && frame.pads[i].ly==input.pads[i].ly,"independent player states");
    for(size_t i=0;i<encoded.size();++i) {
        auto changed=encoded; changed[i]^=1;
        check(rejects([&] { decode_packet(changed,key); }),"tampering with any byte is rejected");
    }
    check(rejects([&] { decode_packet(encoded,pairing_key("wrong-unit-key")); }),"wrong pairing code rejected");
    for(size_t i=0;i<encoded.size();++i)
        check(rejects([&] { decode_packet(std::span(encoded.data(),i),key); }),"truncated packets rejected");
    auto bad=packet; bad.payload.resize(kMaxPayload+1);
    check(rejects([&] { encode_packet(bad,key); }),"oversized payload rejected before allocation");
    input.pads[0].lx=40000;
    check(rejects([&] { encode_commit(input); }),"out-of-range axes rejected");
    check(private_ipv4(0x7f000001)&&private_ipv4(0xc0a80101)&&private_ipv4(0x64400002)&&private_ipv4(0xac1f0001),"loopback, LAN and private VPN ranges");
    check(!private_ipv4(0x08080808)&&!private_ipv4(0xac200001)&&!private_ipv4(0x643fffff)&&!private_ipv4(0),"public and unspecified peer addresses rejected");
    std::puts("native LAN wire: four-player input, authentication, bounds and private-address tests passed");
}
