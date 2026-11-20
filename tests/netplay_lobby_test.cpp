#include "netplay_game.h"
#include "netplay_wire.h"
#include <nx/runtime.h>
#include <nx/game.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>

static std::string data;
namespace nx {
std::atomic<uint64_t> g_frame{0};
// This lobby-only fixture loads no guest modules; any query is unexpected.
const LoadedModule* module_at(uint64_t) { std::abort(); }
namespace ft { void input_sample(uint64_t) {} void input_sample_at(uint64_t,uint64_t) {} }
void log(const char*,...) {}
void fatal(const char*,...) { std::abort(); }
namespace game { const Info kInfo={"wonder","test",0,"test","",0}; }
namespace plat {
std::string data_root() { return data; }
uint64_t monotonic_ns() { return uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()); }
void read_pad(PadState&) {}
void controller_force_primary(bool) {}
void sleep_ns(int64_t ns) { std::this_thread::sleep_for(std::chrono::nanoseconds(ns)); }
bool headless() { return true; }
void exit_process(int) { std::abort(); }
}
namespace play {
void pad(plat::PadState&) {}
uint64_t netplay_state_hash() { return 0; }
lan::PlayerState netplay_player_state() { return {}; }
}
}
static void check(bool ok,const char* message) { if(!ok) { std::fprintf(stderr,"%s\n",message); std::exit(1); } }
static nx::lan::LobbyStatus wait(nx::lan::LobbyPhase wanted) {
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(std::chrono::steady_clock::now()<until) {
        const auto state=nx::lan::lobby_status();
        if(state.phase==wanted) return state;
        if(state.phase==nx::lan::LobbyPhase::Failed) { std::fprintf(stderr,"lobby: %s\n",state.detail.c_str()); break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(false,"lobby reached its expected state before the deadline"); return {};
}
int main() {
    namespace fs=std::filesystem; using namespace nx::lan;
    const auto root=fs::temp_directory_path()/("wonder-lan-lobby-test-"+std::to_string(nx::plat::monotonic_ns()));
    const auto encoded=root.u8string(); data.assign(reinterpret_cast<const char*>(encoded.data()),encoded.size());
    fs::create_directories(root/"analysis"); fs::create_directory(root/"game");
    for(const auto* name:{"main.bin","sdk.bin"}) { std::ofstream out(root/"analysis"/name); out<<name; }
    { std::ofstream out(root/"game"/"build-id.txt"); out<<"synthetic lobby fixture"; }
    #if defined(_WIN32)
    { std::ofstream out(root/"game"/"wonder_game.dll"); out<<"synthetic game library"; }
#else
    { std::ofstream out(root/"game"/"libwonder_game.so"); out<<"synthetic game library"; }
#endif
    LobbyRequest request{true,"127.0.0.1","lobby-cancel-retry-test",0,2};
    check(start_lobby(request),"room opens asynchronously");
    const auto opened=wait(LobbyPhase::Waiting);
    check(opened.listening_port!=0,"OS-assigned room port is reported");
    check(!start_lobby(request),"a second start cannot race a live room");
    cancel_lobby(); wait(LobbyPhase::Failed);
    request.port=opened.listening_port;
    check(start_lobby(request),"a cancelled room can be retried");
    const auto retried=wait(LobbyPhase::Waiting);
    check(retried.listening_port==request.port,"cancellation releases the original listening socket");
    cancel_lobby(); wait(LobbyPhase::Failed);
    check(!active(),"cancelled lobby cannot activate game inputs");
    check(fs::weakly_canonical(root).parent_path()==fs::weakly_canonical(fs::temp_directory_path()) && root.filename().string().starts_with("wonder-lan-lobby-test-"),"cleanup stays within this test's temporary directory");
    fs::remove_all(root); std::puts("native LAN lobby: asynchronous start, cancellation, retry and socket release passed");
}
