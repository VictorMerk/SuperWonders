#include "netplay_session.h"
#include "netplay_save.h"
#include "netplay_pipeline.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

static void check(bool ok,const char* message) {
    if(!ok) { std::fprintf(stderr,"%s\n",message); std::exit(1); }
}
static nx::plat::PadState pad(uint8_t role,uint64_t tick) {
    return {uint64_t(1)<<(role+4),int32_t(role*100+tick),-int32_t(role*200+tick),int32_t(tick),-int32_t(tick)};
}
static void verify(const nx::lan::FrameInputs& frame,uint8_t count,uint64_t tick) {
    check(frame.tick==tick&&frame.players==count&&frame.state_hash==tick+1,"committed tick and state hash");
    for(uint8_t role=0;role<count;++role) {
        const auto expected=pad(role,tick); const auto& actual=frame.pads[role];
        check(actual.buttons==expected.buttons&&actual.lx==expected.lx&&actual.ly==expected.ly&&actual.rx==expected.rx&&actual.ry==expected.ry,
            "every peer receives the same independent player inputs");
    }
}
static nx::lan::SessionOptions options(uint8_t count) {
    nx::lan::SessionOptions out; out.players=count; out.port=0;
    out.key=nx::lan::pairing_key("native-session-test-code"); out.build[0]=41; out.save[0]=99;
    out.epoch=1700000000; out.seed=0x123456789abcdef;
    out.timeout=std::chrono::seconds(5); return out;
}
static void exchange_route(nx::lan::Session& session,uint8_t count,uint8_t role,bool overlap) {
    using namespace nx::lan;
    if (!overlap) {
        for(uint64_t tick=0;tick<64;++tick) verify(session.exchange(pad(role,tick),tick+1),count,tick);
        return;
    }
    OneAhead<uint64_t,FrameInputs> pipeline([&](uint64_t tick) { return session.exchange(pad(role,tick),tick+1); },[&] { session.stop(); });
    for(uint64_t tick=0;tick<64;++tick) {
        const auto previous=pipeline.submit(tick);
        if (tick==0) check(!previous,"pipelined session has an explicit neutral lead-in");
        else { check(previous.has_value(),"pipelined input is available in exact order"); verify(*previous,count,tick-1); }
    }
    verify(*pipeline.receive(),count,63);
}
static void roundtrip(uint8_t count,bool overlap=false) {
    using namespace nx::lan;
    Session host; auto config=options(count);
    std::promise<uint16_t> listening; auto port=listening.get_future();
    std::mutex lock; std::vector<std::string> errors;
    auto error=[&](const std::exception& e) { std::lock_guard guard(lock); errors.emplace_back(e.what()); };
    std::thread server([&] {
        try {
            host.host(config,[&](uint16_t number) { listening.set_value(number); });
            exchange_route(host,count,0,overlap);
        } catch(const std::exception& e) { error(e); }
    });
    check(port.wait_for(std::chrono::seconds(5))==std::future_status::ready,"host begins listening");
    config.port=port.get();
    // Bad authentication and incompatible saves must not occupy a player slot.
    for(bool wrong_key : {true,false}) {
        Session rejected; auto bad=config;
        if(wrong_key) bad.key=pairing_key("wrong-native-session-test-code"); else bad.save[0]^=1;
        bool failed=false; try { rejected.join(bad); } catch(...) { failed=true; }
        check(failed,"wrong key or save is rejected before starting a game");
    }
    std::vector<std::thread> clients;
    for(uint8_t i=1;i<count;++i) clients.emplace_back([&] {
        try {
            Session client; client.join(config);
            check(client.players()==count&&client.role()>0&&client.role()<count,"client assigned an independent slot");
            check(client.epoch()==config.epoch&&client.seed()==config.seed,"all peers share the session clock origin and seed");
            exchange_route(client,count,client.role(),overlap);
        } catch(const std::exception& e) { error(e); }
    });
    for(auto& client:clients) client.join(); server.join();
    for(const auto& text:errors) std::fprintf(stderr,"%s\n",text.c_str());
    check(errors.empty(),"real TCP lockstep exchange succeeds");
}
static void divergence(bool pose = false) {
    using namespace nx::lan;
    Session host; auto config=options(2);
    std::promise<uint16_t> listening; auto port=listening.get_future();
    std::atomic<bool> host_rejected{false},client_rejected{false};
    std::atomic<unsigned> host_frames{0},client_frames{0};
    std::thread server([&] {
        try {
            host.host(config,[&](uint16_t p) { listening.set_value(p); });
            for(uint64_t tick=0;tick<8;++tick) { host.exchange(pad(0,tick),tick+1,{42,true,{19.5f,91.0f,0.0f}}); ++host_frames; }
        } catch(...) { host_rejected=true; }
    });
    check(port.wait_for(std::chrono::seconds(5))==std::future_status::ready,"divergence host starts");
    config.port=port.get();
    std::thread guest([&] {
        try {
            Session client; client.join(config);
            for(uint64_t tick=0;tick<8;++tick) {
                PlayerState state{42,true,{19.5f,91.0f,0.0f}};
                if(pose && tick==3) state.position[0]+=0.1f;
                client.exchange(pad(1,tick),!pose && tick==3?0xabcdef:tick+1,state); ++client_frames;
            }
        } catch(...) { client_rejected=true; }
    });
    guest.join(); server.join();
    check(host_rejected&&client_rejected&&host_frames==3&&client_frames==3,"divergence stops both peers before another input frame is committed");
}
static void cancellation() {
    using namespace nx::lan;
    Session host; auto config=options(2); std::promise<uint16_t> listening; auto port=listening.get_future();
    std::atomic<bool> cancelled{false};
    std::thread server([&] {
        try { host.host(config,[&](uint16_t p) { listening.set_value(p); }); }
        catch(...) { cancelled=true; }
    });
    check(port.wait_for(std::chrono::seconds(5))==std::future_status::ready,"cancellation host starts");
    host.stop(); server.join(); check(cancelled,"stopping a room interrupts its accept wait");
}
static void save_transfer() {
    using namespace nx::lan;
    Session host; auto config=options(2);
    config.allow_save_transfer=true;
    config.initial_save.resize(65537);
    for(size_t i=0;i<config.initial_save.size();++i) config.initial_save[i]=uint8_t(i%251);
    config.save=digest(config.initial_save);
    std::promise<uint16_t> listening; auto port=listening.get_future();
    std::atomic<bool> host_ok{false},client_ok{false},installed{false};
    std::thread server([&] {
        try {
            host.host(config,[&](uint16_t p){listening.set_value(p);});
            verify(host.exchange(pad(0,0),1),2,0); host_ok=true;
        } catch(const std::exception& e) { std::fprintf(stderr,"save host: %s\n",e.what()); }
    });
    check(port.wait_for(std::chrono::seconds(5))==std::future_status::ready,"save transfer host starts");
    auto guest_config=options(2); guest_config.port=port.get(); guest_config.allow_save_transfer=true;
    guest_config.save[0]^=1;  // different normal client progress is allowed
    guest_config.install_save=[&](std::span<const uint8_t> bytes) {
        check(bytes.size()==config.initial_save.size() && std::equal(bytes.begin(),bytes.end(),config.initial_save.begin()),"authenticated multi-packet snapshot preserves every byte");
        installed=true;
    };
    try {
        Session guest; guest.join(guest_config);
        check(installed,"session cannot start before isolated save installation");
        verify(guest.exchange(pad(1,0),1),2,0); client_ok=true;
    } catch(const std::exception& e) { std::fprintf(stderr,"save client: %s\n",e.what()); host.stop(); }
    server.join(); check(host_ok&&client_ok&&installed,"host-save transfer starts a session with matching state");
}
int main() {
    roundtrip(2); roundtrip(4); roundtrip(2,true); roundtrip(4,true); divergence(); divergence(true); cancellation(); save_transfer();
    std::puts("native LAN TCP: serial/pipelined 2/4 peers, authentication, saves, shared inputs, divergence and cancellation passed");
}
