#include "profiles.h"
#include "netplay_game.h"
#include "netplay_content.h"
#include "netplay_session.h"
#include "netplay_save.h"
#include "netplay_trace.h"
#include "netplay_pipeline.h"
#include "controllers.h"
#include "play.h"
#include "frametrace.h"
#include "launcher/update_crypto.h"
#include <nx/runtime.h>
#include <nx/game.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace nx::lan {
namespace {
namespace fs = std::filesystem;
std::atomic<Session*> session{nullptr};  // live gameplay connection remains valid until process shutdown
std::mutex connection_lock;
std::atomic<bool> running{false};
std::atomic<bool> stopping{false};
constinit std::atomic<TickSeedFilter> tick_seed_filter{nullptr};
std::mutex input_lock;
FrameInputs inputs;
LockstepTiming lockstep_timing;
std::array<LockstepTiming,kPlayers> peer_work;
FrameWorkClock frame_work_clock;
fs::path& isolated_user = *new fs::path;
uint64_t current_epoch = 0, current_seed = 0;
uint32_t role = 0, count = 0;
InputTrace<>& input_trace = *new InputTrace<>;
std::atomic<bool> trace_started{false};
struct RecordedExchange { FrameInputs inputs; uint64_t sampled_ns, partial_hash; bool position_valid; };
std::atomic<uint64_t> pending_loading_event{0};
using ExchangePipeline=OneAhead<TraceEntry,RecordedExchange>;
ExchangePipeline* exchange_pipeline=nullptr;
uint64_t scheduled_tick=1;
std::atomic<bool> use_pipeline{false};
std::atomic<uint32_t> current_rate{60};
bool pipeline_enabled() { return use_pipeline.load(); }

void log_network_diagnostics();
void dump_trace() noexcept {
    if (!trace_started.load()) return;
    try {
        log_network_diagnostics();
        const auto data=fs::path(reinterpret_cast<const char8_t*>(plat::data_root().c_str()));
        std::ofstream out(data/"lan-input-trace.csv",std::ios::binary|std::ios::trunc);
        input_trace.write(out,role,current_seed,pipeline_enabled()?1:0,current_rate.load());
        if (!out) log("lan: could not save the diagnostic input trace");
        if(const auto* connection=session.load()) {
            const auto d=connection->diagnostics();
            std::ofstream net(data/"lan-transport-stats.json",std::ios::binary|std::ios::trunc);
            net.imbue(std::locale::classic());
            net << "{\n\"schema\":1,\"protocol\":" << kProtocol << ",\"role\":" << role
                << ",\"simulation_hz\":" << current_rate.load() << ",\"input_lead\":" << (pipeline_enabled()?1:0)
                << ",\"sent_wire_bytes\":" << d.sent_bytes << ",\"received_wire_bytes\":" << d.received_bytes << ",\n\"messages\":{";
            constexpr const char* names[]={"unused","hello","welcome","start","reject","input","commit","ack","leave","save_begin","save_chunk","save_end","save_ack"};
            static_assert(std::size(names)==kDiagnosticMessageTypes);
            for(size_t i=1;i<kDiagnosticMessageTypes;++i) {
                if(i>1)net << ',';
                net << '\"' << names[i] << "\":{\"sent\":" << d.sent_messages[i] << ",\"mac_valid_received\":" << d.received_messages[i] << '}';
            }
            net << "},\n\"socket_waits\":{\"send\":" << d.send_waits << ",\"receive\":" << d.receive_waits
                << ",\"total_ns\":" << d.wait_ns << ",\"peak_select_ns\":" << d.peak_wait_ns << "},\n\"errors\":{\"timeouts_total\":" << d.timeouts
                << ",\"receive_timeouts_subset\":" << d.receive_timeouts << ",\"cancellations\":" << d.cancellations
                << ",\"eof_disconnects\":" << d.disconnects << ",\"socket_errors\":" << d.socket_errors << ",\"invalid_packets\":" << d.invalid_packets
                << "},\n\"scope\":\"Non-transactional atomic transport snapshot; bytes exclude TCP/IP, MAC validity is not world-state agreement, socket waits are not RTT\"}\n";
            if(!net)log("lan: could not save transport diagnostics");
        }

    } catch (...) { log("lan: could not save the diagnostic input trace"); }
}
struct Lobby {
    std::mutex lock;
    LobbyStatus status;
    std::atomic<bool> cancel{false};
};
Lobby& lobby() { static auto* state=new Lobby; return *state; }
void status(LobbyPhase phase,std::string detail,uint16_t port=0) {
    std::lock_guard lock(lobby().lock); lobby().status={phase,std::move(detail),port};
}
void register_shutdown() {
    static const bool registered=[] {
        std::atexit([] { flush_diagnostics(); });
        return true;
    }();
}
std::string value(const char* name, const char* fallback = "") {
    const auto* e = std::getenv(name); return e ? e : fallback;
}
void preparation_checkpoint() {
    if(lobby().cancel) throw std::runtime_error("LAN connection cancelled");
}
Fingerprint fingerprint(const fs::path& data) {
#if defined(_WIN32)
    std::wstring executable(32768,L'\0');
    const auto n=GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));
    if(!n || n>=executable.size()) throw std::runtime_error("Could not identify the LAN runtime build");
    executable.resize(n);
    const auto runtime=fs::path(executable);
#else
    const auto runtime=fs::read_symlink("/proc/self/exe");
#endif
    return content_fingerprint(data,runtime,NX_VERSION_STRING,value("NX_ASPECT_PROFILE"),preparation_checkpoint);
}
void isolate_client_save(const fs::path& data) {
    const auto sessions = data/"lan-sessions";
    fs::create_directories(sessions);
    for (uint32_t attempt=0;attempt<100;++attempt) {
        const auto name = "client-"+std::to_string(plat::monotonic_ns())+"-"+std::to_string(attempt);
        const auto destination = sessions/name;
        if (!fs::create_directory(destination)) continue;
        isolated_user = destination/"user";
        fs::create_directories(isolated_user);
        return;
    }
    throw std::runtime_error("Could not create an isolated LAN client profile");
}
[[noreturn]] void end_session(const std::string& reason) {
    // A voluntary process shutdown already owns cleanup. Cancelling its pending
    // input exchange must not launch another cleanup or write a spurious error.
    if (stopping.load()) for (;;) plat::sleep_ns(1'000'000'000);
    log("lan: session ended: %s",reason.c_str());
    dump_trace();  // headless exit and platform shutdown need not run atexit
    if(plat::headless()) plat::exit_process(2);
    try {
        const auto data=fs::path(reinterpret_cast<const char8_t*>(plat::data_root().c_str()));
        std::ofstream note(data/"lan-last-session.txt",std::ios::binary);
        note<<"LAN session ended: "<<reason.substr(0,1500)<<" Reconnect from a new room.";
    } catch(...) {}
#if defined(_WIN32)
    std::wstring self(32768,L'\0');
    const auto n=GetModuleFileNameW(nullptr,self.data(),DWORD(self.size()));
    if(n && n<self.size()) {
        self.resize(n);
        auto executable=fs::path(self).parent_path()/L"wonder-launcher.exe";
        std::error_code ec;
        if(!fs::is_regular_file(executable,ec)) executable=fs::path(self);
        auto command=L"\""+executable.wstring()+L"\" --config --lan-ended --launcher-parent="+std::to_wstring(GetCurrentProcessId());
        STARTUPINFOW startup{}; startup.cb=sizeof startup;
        PROCESS_INFORMATION process{};
        if(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)) {
            CloseHandle(process.hThread); CloseHandle(process.hProcess);
        }
    }
#else
    const auto self=fs::read_symlink("/proc/self/exe").string();
    if(fork()==0) { execl(self.c_str(),self.c_str(),"--config","--lan-ended",nullptr); _exit(127); }
#endif
    plat::exit_process(2);
}
void log_network_diagnostics() {
    const auto* connection=session.load();
    if(!connection)return;
    const auto d=connection->diagnostics();
    const auto input=size_t(Message::Input),commit=size_t(Message::Commit);
    log("lan transport: tx %llu bytes, MAC-valid rx %llu input/%llu commit messages; tx input/commit %llu/%llu, rx wire %llu bytes; socket waits send/recv %llu/%llu, total %.2f ms, peak select %.2f ms (not RTT)",
        (unsigned long long)d.sent_bytes,(unsigned long long)d.received_messages[input],(unsigned long long)d.received_messages[commit],
        (unsigned long long)d.sent_messages[input],(unsigned long long)d.sent_messages[commit],(unsigned long long)d.received_bytes,
        (unsigned long long)d.send_waits,(unsigned long long)d.receive_waits,double(d.wait_ns)/1e6,double(d.peak_wait_ns)/1e6);
    log("lan transport errors: timeouts %llu (receive %llu), cancellations %llu, EOF disconnects %llu, socket errors %llu, invalid packets %llu",
        (unsigned long long)d.timeouts,(unsigned long long)d.receive_timeouts,(unsigned long long)d.cancellations,
        (unsigned long long)d.disconnects,(unsigned long long)d.socket_errors,(unsigned long long)d.invalid_packets);
}
RecordedExchange perform_exchange(TraceEntry entry) {
    const auto started=plat::monotonic_ns();
    entry.exchange_started_ns=started;
    entry.loading_event=pending_loading_event.load();  // exactly the marker sent by this exchange
    FrameInputs agreed;
    try { agreed=session.load()->exchange(entry.local,0,entry.state,entry.local_work_ns,entry.loading_event); }
    catch (...) {
        entry.exchange_finished_ns=plat::monotonic_ns();
        entry.wait_ns=entry.exchange_finished_ns-started;
        input_trace.push(entry);
        throw;
    }
    entry.tick=agreed.tick; entry.agreed=agreed; entry.committed=true;
    entry.exchange_finished_ns=plat::monotonic_ns();
    entry.wait_ns=entry.exchange_finished_ns-started;
    input_trace.push(entry);
    return {agreed,entry.sampled_ns,entry.partial_hash,entry.state.valid && session.load()->position_probe_ready()};
}
void exchange() {
    const auto started=plat::monotonic_ns();
    const auto local_work_ns=std::min(frame_work_clock.elapsed(started),kMaxFrameWorkNs);
    auto sample=[&] {
        TraceEntry entry;
        plat::read_pad(entry.local); play::pad(entry.local);
        entry.sampled_ns=plat::monotonic_ns();
        entry.tick=scheduled_tick++; entry.frame=g_frame.load();
        entry.state=play::netplay_player_state(); entry.partial_hash=play::netplay_state_hash();
        entry.local_work_ns=local_work_ns;
        return entry;
    };
    std::optional<RecordedExchange> completed;
    try {
        if (exchange_pipeline) completed=exchange_pipeline->submit_sampled(sample);
        else completed=perform_exchange(sample());
    } catch (const std::exception& e) {
        running=false;
        end_session(e.what());
    }
    const auto finished=plat::monotonic_ns();
    lockstep_timing.record(finished-started);
    frame_work_clock.finish(finished);  // the next simulation/render/pacing interval starts here
    if (!completed) return;  // explicit neutral lead-in
    inputs=completed->inputs;
    for(size_t player=0;player<count;++player)
        if(inputs.frame_work_ns[player]) peer_work[player].record(inputs.frame_work_ns[player]);
    ft::input_sample_at(g_frame,completed->sampled_ns);
    if (inputs.tick % 600 == 0) {
        log_network_diagnostics();
        log("lan: synchronized input tick %llu, local player %u of %u, partial player-state hash %llx",
            (unsigned long long)inputs.tick,role+1,count,(unsigned long long)completed->partial_hash);
        const auto stats=lockstep_timing.snapshot();
        log("lan: input barrier %.2f ms smoothed, %.2f ms peak, %llu waits over 50 ms (not network RTT)",
            stats.smoothed_ms,stats.peak_ms,(unsigned long long)stats.waits_over_50_ms);
        if (!completed->position_valid)
            log("lan: player probe has no agreed baseline; this scene, character choices and world state are not verified");
        for(uint32_t player=0;player<count;++player) {
            const auto stats=peer_work[player].snapshot();
            if(stats.ticks) log("lan: PC %u frame outside LAN wait %.2f ms smoothed, %.2f ms peak (includes pacing/render/scheduling)",
                player+1,stats.smoothed_ms,stats.peak_ms);
        }
    }
}
}
namespace {
void connect_game(const LobbyRequest& request) {
    if (std::string(game::kInfo.id)!="wonder") throw std::runtime_error("Native LAN currently supports Wonder only");
    const auto data=fs::path(reinterpret_cast<const char8_t*>(plat::data_root().c_str()));
    isolated_user.clear();
    SessionOptions options;
    options.key=pairing_key(request.code);
    options.address=request.address; options.port=request.port; options.players=request.players;
    options.input_lead=(request.pipeline || value("NX_LAN_PIPELINE")=="1")?1:0;
    options.frame_rate=request.frame_rate;
    if (request.host) {
        const auto rate=value("NX_LAN_RATE");
        if (!rate.empty()) {
            if (rate!="30" && rate!="60") throw std::runtime_error("The LAN simulation target must be 30 or 60 Hz");
            options.frame_rate=rate=="30"?30:60;
        }
    }
    if(!options.port && !request.host) throw std::runtime_error("The LAN port must be between 1 and 65535");
    options.probe_warmup=4;  // The discovery probe has no verified actor identity.
    options.timeout=std::chrono::minutes(3);
    if(request.host && value("NX_TEST_VIRTUAL_ONLY")=="1") {
        const auto test_seed=value("NX_LAN_TEST_SEED");
        if(!test_seed.empty()) {
            size_t consumed=0;
            options.seed=std::stoull(test_seed,&consumed);
            if(consumed!=test_seed.size() || !options.seed) throw std::runtime_error("Invalid LAN test seed");
        }
    }
    options.idle=[] { return !lobby().cancel.load(); };
    status(LobbyPhase::Preparing,"Checking the game build and mods...");
    options.build=fingerprint(data);
    options.allow_save_transfer=true;
    if(request.host) {
        status(LobbyPhase::Preparing,"Copying the host save for this session...");
        options.initial_save=capture_save(profiles::user_root(data)/"save",preparation_checkpoint);
        options.save=digest(options.initial_save);
    } else {
        isolate_client_save(data);
        options.install_save=[](std::span<const uint8_t> snapshot) {
            status(LobbyPhase::Waiting,"Preparing a separate copy of the host's save...");
            const auto pending=isolated_user/"save.partial";
            restore_save(snapshot,pending,preparation_checkpoint); fs::rename(pending,isolated_user/"save");
        };
    }
    if(lobby().cancel) throw std::runtime_error("LAN connection cancelled");
    auto* connection=new Session;
    { std::lock_guard lock(connection_lock); session.store(connection); }
    register_shutdown();
    log("lan: starting %s, waiting for %u independent native instances",request.host?"host":"join",unsigned(request.players));
    if(request.host) connection->host(options,[](uint16_t port) {
        log("lan: listening on private LAN TCP port %u",unsigned(port));
        status(LobbyPhase::Waiting,"Room open. Waiting for the other players...",port);
    });
    else {
        status(LobbyPhase::Waiting,"Connecting to the host and receiving the session save...");
        connection->join(options);
    }
    role=connection->role(); count=connection->players(); current_epoch=connection->epoch(); current_seed=connection->seed();
    current_rate=connection->frame_rate(); use_pipeline=connection->input_lead()!=0;
    log("lan: host timing adopted: %u Hz, %u buffered input frames",unsigned(current_rate.load()),unsigned(connection->input_lead()));
    log("lan: session seed %llu, epoch %llu, protocol %u",(unsigned long long)current_seed,(unsigned long long)current_epoch,unsigned(kProtocol));
    lockstep_timing.reset();
    for(auto& timing:peer_work) timing.reset();
    frame_work_clock.reset();
    // Every peer starts with a neutral frame. The UI's confirmation button is not
    // sampled on this background thread and cannot leak into the game's input.
    inputs=connection->exchange({});
    input_trace.reset(); trace_started=true;
    // Retain room metadata even if startup fails before the first game frame.
    // This is an agreed neutral input, not a sampled physical controller input.
    TraceEntry initial; initial.tick=inputs.tick; initial.agreed=inputs; initial.committed=true;
    input_trace.push(initial);
    scheduled_tick=inputs.tick+1;
    if (pipeline_enabled()) {
        exchange_pipeline=new ExchangePipeline(perform_exchange,[] { if(auto* connection=session.load()) connection->stop(); });
        log("lan: experimental one-frame input lead; network exchange overlaps simulation; not zero input latency");
    }

    plat::controller_force_primary(true);
    running=true;
    status(LobbyPhase::Ready,"All players connected. Starting Wonder...");
}
}
void initialize() {
    if(active()) return;  // a launcher lobby already owns the live connection
    const auto mode=value("NX_LAN_MODE");
    if(mode.empty()) return;
    if(mode!="host" && mode!="join") fatal("NX_LAN_MODE must be host or join");
    try {
        const auto port=std::stoul(value("NX_LAN_PORT","27846"));
        const auto players=std::stoul(value("NX_LAN_PLAYERS","2"));
        if(!port || port>65535 || players<2 || players>4) throw std::runtime_error("LAN port/player count is invalid");
        lobby().cancel=false;
        connect_game({mode=="host",value("NX_LAN_ADDRESS",mode=="host"?"0.0.0.0":"127.0.0.1"),value("NX_LAN_CODE"),uint16_t(port),uint8_t(players)});
    } catch(const std::exception& e) { fatal("Could not start native LAN: %s",e.what()); }
}
bool start_lobby(LobbyRequest request) {
    {
        std::lock_guard lock(lobby().lock);
        const auto phase=lobby().status.phase;
        if(active() || phase==LobbyPhase::Preparing || phase==LobbyPhase::Waiting || phase==LobbyPhase::Ready) return false;
        lobby().cancel=false; lobby().status={LobbyPhase::Preparing,"Preparing the LAN session..."};
    }
    register_shutdown();
    try {
        std::thread([request=std::move(request)] {
            try { connect_game(request); }
            catch(const std::exception& e) {
                running=false;
                { std::lock_guard lock(connection_lock); delete session.exchange(nullptr); }
                log("lan: lobby failed: %s",e.what());
                status(LobbyPhase::Failed,e.what());
            }
        }).detach();
    } catch(const std::exception& e) { status(LobbyPhase::Failed,e.what()); return false; }
    return true;
}
void cancel_lobby() {
    lobby().cancel=true;
    std::lock_guard lock(connection_lock);
    if(auto* connection=session.load()) connection->stop();
}
LobbyStatus lobby_status() { std::lock_guard lock(lobby().lock); return lobby().status; }
bool active() { return running.load(); }
void set_tick_seed_filter(TickSeedFilter filter) { tick_seed_filter.store(filter,std::memory_order_release); }
uint64_t guest_tick(uint64_t caller,uint64_t real_tick) {
    if (active()) if (auto filter=tick_seed_filter.load(std::memory_order_acquire)) return filter(caller,real_tick);
    return real_tick;
}
bool connected(uint32_t player) { return active() && player<count; }
plat::PadState pad(uint32_t player) {
    std::lock_guard guard(input_lock);
    return connected(player) ? inputs.pads[player] : plat::PadState{};
}
void next_frame() { if(active()) { std::lock_guard guard(input_lock); exchange(); } }
void test_file_delay(const char* path) {
    static const bool enabled=[] {const char* flag=std::getenv("NX_TEST_VIRTUAL_ONLY");return flag && !std::strcmp(flag,"1");}();
    if(!enabled || !active()) return;
    // Unattended loading regression only: hold one selected loader before it
    // announces readiness, while the other peer keeps exchanging frames.
    static std::atomic<bool> delayed{false};
    const auto delay=value("NX_LAN_TEST_LOADING_DELAY_MS");
    const auto match=value("NX_LAN_TEST_LOADING_EVENT");
    if(!delay.empty() && !match.empty() &&
       std::strstr(path,match.c_str()) && !delayed.exchange(true)) {
        size_t used=0;const auto milliseconds=std::stoul(delay,&used);
        if(used!=delay.size() || milliseconds>20000) throw std::runtime_error("Invalid LAN test loading delay");
        log("lan test: holding loader for %lu ms before %s",milliseconds,path);
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
}
void loading_event(const char* path) {
    if(!active()) return;
    uint64_t event=0xcbf29ce484222325ull;
    for(const unsigned char* p=reinterpret_cast<const unsigned char*>(path);*p;++p) event=(event^*p)*0x100000001b3ull;
    if(!event) event=1;
    std::lock_guard guard(input_lock);
    try {
        // Drain the outstanding worker before issuing exchanges on this thread.
        if(exchange_pipeline) exchange_pipeline->receive();
        pending_loading_event=event;
        log("lan: waiting at loading event %llx (%s)",(unsigned long long)event,path);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(10);
        for(;;) {
            TraceEntry entry; entry.tick=scheduled_tick++; entry.frame=g_frame.load(); entry.sampled_ns=plat::monotonic_ns();
            const auto agreed=perform_exchange(entry).inputs;
            bool all=true;
            for(uint32_t player=0;player<count;++player) {
                if(agreed.loading_events[player] && agreed.loading_events[player]!=event)
                    throw std::runtime_error("LAN loading events differ; restart from the same session state");
                all=all && agreed.loading_events[player]==event;
            }
            if(all) { inputs={}; inputs.players=uint8_t(count); inputs.tick=agreed.tick; break; }
            if(std::chrono::steady_clock::now()>deadline) throw std::runtime_error("LAN scene loading timed out");
        }
        pending_loading_event=0;
        frame_work_clock.reset();
        log("lan: loading event agreed %llx",(unsigned long long)event);
    } catch(const std::exception& error) { running=false; end_session(error.what()); }
}
uint64_t seed() { return current_seed; }
uint64_t epoch() { return current_epoch; }
uint32_t local_player() { return role; }
uint32_t simulation_rate() { return current_rate.load(); }
bool player_probe_ready() {
    const auto* connection=session.load();
    return active() && connection && connection->position_probe_ready();
}
TimingSnapshot timing() { return lockstep_timing.snapshot(); }
PeerFrameTiming peer_timing(uint32_t player) {
    if(player>=kPlayers || !active()) return {};
    const auto value=peer_work[player].snapshot();
    return {value.ticks,value.last_ms,value.smoothed_ms,value.peak_ms};
}
void flush_diagnostics() noexcept {
    stopping=true; running=false;
    std::lock_guard guard(connection_lock);
    if (auto* connection=session.load()) connection->stop();
    if (exchange_pipeline) exchange_pipeline->stop();
    dump_trace();
}
fs::path user_root(const fs::path& data) { return isolated_user.empty() ? profiles::user_root(data) : isolated_user; }
}
