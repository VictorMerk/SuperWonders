#include "netplay_game.h"
#include "netplay_session.h"
#include "netplay_save.h"
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
std::mutex input_lock;
FrameInputs inputs;
LockstepTiming lockstep_timing;
fs::path& isolated_user = *new fs::path;
uint64_t current_epoch = 0, current_seed = 0;
uint32_t role = 0, count = 0;
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
        std::atexit([] { std::lock_guard lock(connection_lock); if(auto* connection=session.load()) connection->stop(); });
        return true;
    }();
}
std::string value(const char* name, const char* fallback = "") {
    const auto* e = std::getenv(name); return e ? e : fallback;
}
void hash_file(UcSha256& hash, const fs::path& path) {
    std::ifstream in(path,std::ios::binary);
    if (!in) throw std::runtime_error("Could not read LAN compatibility data");
    std::array<char,65536> block{};
    while (in) {
        in.read(block.data(),block.size());
        if (in.gcount()) uc_sha256_update(&hash,block.data(),size_t(in.gcount()));
    }
    if (!in.eof()) throw std::runtime_error("Reading LAN compatibility data failed");
}
void hash_tree(UcSha256& hash, const fs::path& root, uint64_t limit) {
    if (!fs::exists(root)) return;
    std::vector<fs::path> files;
    uint64_t total = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_symlink()) throw std::runtime_error("LAN compatibility folders cannot contain symbolic links");
        if (!entry.is_regular_file()) continue;
        const auto size = entry.file_size();
        if (size > limit-total || files.size() >= 65536) throw std::runtime_error("LAN compatibility data exceeds the supported limit");
        total += size; files.push_back(fs::relative(entry.path(),root));
    }
    std::sort(files.begin(),files.end());
    for (const auto& file : files) {
        const auto name = file.generic_u8string();
        // Include canonical path and length boundaries, never filesystem metadata.
        std::array<uint8_t,16> sizes{};
        uint64_t n = name.size(), size = fs::file_size(root/file);
        for (int i=0;i<8;++i) { sizes[i]=uint8_t(n); n>>=8; sizes[i+8]=uint8_t(size); size>>=8; }
        uc_sha256_update(&hash,sizes.data(),sizes.size());
        uc_sha256_update(&hash,name.data(),name.size()); hash_file(hash,root/file);
    }
}
Fingerprint fingerprint(const fs::path& data) {
    UcSha256 hash; uc_sha256_init(&hash);
    {
        constexpr char revision[] = "Wonder lockstep development 1; " NX_VERSION_STRING;
        uc_sha256_update(&hash,revision,sizeof revision);
        hash_file(hash,data/"analysis"/"main.bin"); hash_file(hash,data/"analysis"/"sdk.bin");
        hash_file(hash,data/"game"/"build-id.txt");
        hash_tree(hash,data/"mods",1ull<<30);
#if defined(_WIN32)
        std::wstring executable(32768,L'\0');
        const auto n=GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));
        if(!n || n>=executable.size()) throw std::runtime_error("Could not identify the LAN runtime build");
        executable.resize(n); hash_file(hash,fs::path(executable));
#else
        hash_file(hash,fs::read_symlink("/proc/self/exe"));
#endif
    }
    Fingerprint result{}; uc_sha256_final(&hash,result.data()); return result;
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
    log("lan: session ended: %s",reason.c_str());
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
        auto command=L"\""+executable.wstring()+L"\" --config --lan-ended";
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
    plat::exit_process(0);
}
void exchange() {
    plat::PadState local{};
    plat::read_pad(local); play::pad(local);
    ft::input_sample(g_frame);  // includes the following network barrier in diagnostic latency
    const auto started = plat::monotonic_ns();
    try { inputs = session.load()->exchange(local,0,play::netplay_player_state()); }
    catch (const std::exception& e) {
        running = false;
        end_session(e.what());
    }
    lockstep_timing.record(plat::monotonic_ns() - started);
    if (inputs.tick % 600 == 0) {
        log("lan: synchronized input tick %llu, local player %u of %u, partial player-state hash %llx",
            (unsigned long long)inputs.tick,role+1,count,(unsigned long long)play::netplay_state_hash());
        const auto stats = lockstep_timing.snapshot();
        log("lan: input barrier %.2f ms smoothed, %.2f ms peak, %llu waits over 50 ms (not network RTT)",
            stats.smoothed_ms, stats.peak_ms, (unsigned long long)stats.waits_over_50_ms);
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
    if(!options.port && !request.host) throw std::runtime_error("The LAN port must be between 1 and 65535");
    options.timeout=std::chrono::minutes(3);
    options.idle=[] { return !lobby().cancel.load(); };
    status(LobbyPhase::Preparing,"Checking the game build and mods...");
    options.build=fingerprint(data);
    options.allow_save_transfer=true;
    if(request.host) {
        options.initial_save=capture_save(data/"user"/"save");
        options.save=digest(options.initial_save);
    } else {
        isolate_client_save(data);
        options.install_save=[](std::span<const uint8_t> snapshot) {
            status(LobbyPhase::Waiting,"Preparing a separate copy of the host's save...");
            const auto pending=isolated_user/"save.partial";
            restore_save(snapshot,pending); fs::rename(pending,isolated_user/"save");
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
    lockstep_timing.reset();
    // Every peer starts with a neutral frame. The UI's confirmation button is not
    // sampled on this background thread and cannot leak into the game's input.
    inputs=connection->exchange({});
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
bool connected(uint32_t player) { return active() && player<count; }
plat::PadState pad(uint32_t player) {
    std::lock_guard guard(input_lock);
    return connected(player) ? inputs.pads[player] : plat::PadState{};
}
void next_frame() { if(active()) { std::lock_guard guard(input_lock); exchange(); } }
uint64_t seed() { return current_seed; }
uint64_t epoch() { return current_epoch; }
uint32_t local_player() { return role; }
TimingSnapshot timing() { return lockstep_timing.snapshot(); }
fs::path user_root(const fs::path& data) { return isolated_user.empty() ? data/"user" : isolated_user; }
}
