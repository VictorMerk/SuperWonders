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
        // Nonzero timings are used by the transport routes; save-transfer and
        // divergence fixtures retain the explicit unknown (zero) default.
        if(frame.frame_work_ns[role]) check(frame.frame_work_ns[role]==uint64_t(role+1)*1'000'000+tick,"per-PC diagnostics survive the authenticated transport");
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
        for(uint64_t tick=0;tick<64;++tick) {
            const auto frame=session.exchange(pad(role,tick),tick+1,{},uint64_t(role+1)*1'000'000+tick);
            verify(frame,count,tick);
            for(uint8_t player=0;player<count;++player) check(frame.frame_work_ns[player]!=0,"every peer contributes timing evidence");
        }
        return;
    }
    OneAhead<uint64_t,FrameInputs> pipeline([&](uint64_t tick) { return session.exchange(pad(role,tick),tick+1,{},uint64_t(role+1)*1'000'000+tick); },[&] { session.stop(); });
    for(uint64_t tick=0;tick<64;++tick) {
        const auto previous=pipeline.submit(tick);
        if (tick==0) check(!previous,"pipelined session has an explicit neutral lead-in");
        else {
            check(previous.has_value(),"pipelined input is available in exact order"); verify(*previous,count,tick-1);
            for(uint8_t player=0;player<count;++player)
                check(previous->frame_work_ns[player]==uint64_t(player+1)*1'000'000+tick-1,"buffered peer timing is not lost or shifted to another tick");
        }
    }
    verify(*pipeline.receive(),count,63);
}
static void roundtrip(uint8_t count,bool overlap=false,uint8_t rate=60) {
    using namespace nx::lan;
    Session host; auto config=options(count);
    config.frame_rate=rate; config.input_lead=overlap?1:0;
    std::promise<uint16_t> listening; auto port=listening.get_future();
    std::mutex lock; std::vector<std::string> errors;
    auto error=[&](const std::exception& e) { std::lock_guard guard(lock); errors.emplace_back(e.what()); };
    std::thread server([&] {
        try {
            host.host(config,[&](uint16_t number) { listening.set_value(number); });
            check(host.frame_rate()==rate && host.input_lead()==unsigned(overlap),"host timing choices retained");
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
            auto guest_config=config;
            guest_config.frame_rate=rate==60?30:60; guest_config.input_lead=overlap?0:1;
            Session client; client.join(guest_config);
            check(client.frame_rate()==rate && client.input_lead()==unsigned(overlap),"joining peers adopt the authenticated host target/buffer despite different local preferences");
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
static void invalid_timing() {
    using namespace nx::lan;
    for(int which=0;which<6;++which) {
        Session host; auto config=options(2);
        if(which==0) config.frame_rate=0;
        else if(which==1) config.input_lead=2;
        else if(which<4) config.frame_timeout=std::chrono::milliseconds(which==2?99:30001);
        else config.startup_timeout=std::chrono::milliseconds(which==4?99:900001);
        bool failed=false,listened=false;
        try { host.host(config,[&](uint16_t) { listened=true; }); } catch (...) { failed=true; }
        check(failed && !listened,"unsupported simulation/buffer settings fail before opening a room");
    }
}
static void loading_deadline(bool slow_host) {
    using namespace nx::lan;
    Session host,client; auto config=options(2);
    config.timeout=std::chrono::milliseconds(500);
    config.startup_timeout=std::chrono::seconds(2);
    config.frame_timeout=std::chrono::milliseconds(150);
    std::promise<uint16_t> listening;auto port=listening.get_future();
    bool host_ok=false,client_ok=false;
    std::thread server([&] {
        try {
            host.host(config,[&](uint16_t p){listening.set_value(p);});
            host.exchange({});
            if(slow_host)std::this_thread::sleep_for(std::chrono::milliseconds(750));
            host.exchange({});host_ok=true;
        } catch(...) {host.stop();}
    });
    check(port.wait_for(std::chrono::seconds(5))==std::future_status::ready,"loading-test host starts");
    config.port=port.get();
    try {
        client.join(config);client.exchange({});
        if(!slow_host)std::this_thread::sleep_for(std::chrono::milliseconds(750));
        client.exchange({});client_ok=true;
    } catch(...) {client.stop();}
    server.join();
    check(host_ok && client_ok,"initial game loading uses its own deadline, beyond the lobby deadline");
    const auto started=std::chrono::steady_clock::now();bool timed_out=false;
    try {client.exchange({});} catch(const std::runtime_error& e){timed_out=std::string(e.what())=="LAN peer timed out";}
    const auto elapsed=std::chrono::steady_clock::now()-started;
    check(timed_out && elapsed>=config.frame_timeout && elapsed<std::chrono::seconds(1),
          "the longer loading grace must not extend established gameplay stalls");
}
static void stalled_peer(bool host_waits) {
    using namespace nx::lan;
    Session host,client;
    auto config=options(2);
    config.timeout=std::chrono::minutes(3);
    config.frame_timeout=std::chrono::milliseconds(150);
    std::promise<uint16_t> listening;
    auto port=listening.get_future();
    std::thread server([&] { host.host(config,[&](uint16_t p) { listening.set_value(p); }); });
    check(port.wait_for(std::chrono::seconds(5))==std::future_status::ready,"stall-test room starts");
    auto guest=config; guest.port=port.get(); client.join(guest); server.join();
    std::thread warmup([&] {
        host.exchange({});
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        host.exchange({});
    });
    client.exchange({}); client.exchange({}); warmup.join();
    const auto start=std::chrono::steady_clock::now();
    bool timed_out=false;
    try { (host_waits?host:client).exchange({}); }
    catch(const std::runtime_error& e) { timed_out=std::string(e.what())=="LAN peer timed out"; }
    const auto elapsed=std::chrono::steady_clock::now()-start;
    check(timed_out && elapsed>=config.frame_timeout && elapsed<std::chrono::seconds(2),
          "silent connected peer uses the frame deadline, not the three-minute lobby deadline");
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
static void refused_connection() {
    using namespace nx::lan;
    auto config=options(2);
    uint16_t closed_port=0;
    // Destroy a real bound listener, leaving a known unused loopback port.
    {
        Session host;
        try { host.host(config,[&](uint16_t port) { closed_port=port; throw std::runtime_error("close test listener"); }); }
        catch(const std::runtime_error&) {}
    }
    check(closed_port!=0,"refusal fixture bound a port");
    config.port=closed_port; config.timeout=std::chrono::seconds(10);
    const auto start=std::chrono::steady_clock::now();
    bool refused=false;
    try { Session guest; guest.join(config); }
    catch(const std::runtime_error& error) { refused=std::string(error.what()).find("connect")!=std::string::npos; }
    // Windows may retry the SYN for roughly two seconds before reporting refusal.
    check(refused && std::chrono::steady_clock::now()-start<std::chrono::seconds(5),
          "refused Windows connection fails promptly instead of waiting for the lobby deadline");
}
static void unassociated_probe() {
    using namespace nx::lan;
    Session host,client;auto config=options(2);config.probe_warmup=4;
    std::promise<uint16_t> listening;auto port=listening.get_future();
    std::atomic<bool> host_rejected{false},client_rejected{false};
    auto route=[&](Session& session,unsigned role,std::atomic<bool>& rejected){
        for(unsigned tick=0;tick<20;++tick){
            PlayerState state{42,true,{float(tick),2,0}};
            if(tick<8 || tick>=16)state.position[0]+=float(role)*3.75f;
            try {
                const auto frame=session.exchange(pad(uint8_t(role),tick),0,state);
                check(tick<18 && frame.tick==tick && frame.pads[0].buttons==pad(0,tick).buttons && frame.pads[1].buttons==pad(1,tick).buttons,"unassociated probe does not break authenticated shared input");
                check(session.position_probe_ready()==(tick>=11),"both roles establish the same probe baseline");
            }catch(const std::runtime_error& error){
                rejected=tick==18 && std::string(error.what()).find("simulation")!=std::string::npos;return;
            }
        }
    };
    std::thread server([&]{host.host(config,[&](uint16_t p){listening.set_value(p);});route(host,0,host_rejected);});
    check(port.wait_for(std::chrono::seconds(5))==std::future_status::ready,"probe room starts");
    config.port=port.get();client.join(config);route(client,1,client_rejected);server.join();
    check(host_rejected && client_rejected,"both peers stop persistent divergence after probe agreement");
}
int main() {
    for(uint8_t rate:{30,60}) for(uint8_t count:{2,4}) for(bool overlap:{false,true}) roundtrip(count,overlap,rate);
    divergence(); divergence(true); cancellation(); invalid_timing(); save_transfer();
    loading_deadline(true); loading_deadline(false);
    stalled_peer(true); stalled_peer(false);
    refused_connection(); unassociated_probe();
    std::puts("native LAN TCP: serial/pipelined 2/4 peers, authentication, saves, shared inputs, divergence and cancellation passed");
}
