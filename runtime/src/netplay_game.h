#pragma once
#include "profiles.h"
#include "platform.h"
#include "netplay_timing.h"
#include "netplay_input_snapshot.h"
#include <filesystem>
#include <string>
#include <vector>

namespace nx { struct CPU; }

namespace nx::lan {
using TickSeedFilter=uint64_t(*)(uint64_t caller,uint64_t real_tick);
enum class LobbyPhase { Idle, Preparing, Waiting, Ready, Failed };
struct LobbyStatus { LobbyPhase phase = LobbyPhase::Idle; std::string detail; uint16_t listening_port = 0; };
struct LobbyRequest {
    bool host = true;
    std::string address = "127.0.0.1", code;
    uint16_t port = 27846;
    uint8_t players = 2;
    bool pipeline = false;
    uint8_t frame_rate = 60;
};
enum class CalcPhase : unsigned { Pre, Core, Post };
struct FrameworkObservation {
    uint64_t invocation=0, parent=0, frame_before=0;
    uint32_t thread=0;
};
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
// Development opt-in. Transport success alone does not establish simulation sync.
void initialize();
bool active();
bool connected(uint32_t player);
plat::PadState pad(uint32_t player);
InputSnapshot input_snapshot(uint32_t player);
void next_frame();
void trace_framework_frame(uint64_t frame_before,uint64_t frame_after);
bool trace_phases_enabled() noexcept;
FrameworkObservation trace_phase_enter(const CPU&,CalcPhase) noexcept;
void trace_phase_leave(const CPU&,FrameworkObservation,CalcPhase,bool threw=false) noexcept;
FrameworkObservation trace_framework_enter(const CPU&) noexcept;
void trace_framework_leave(const CPU&,FrameworkObservation,bool threw=false) noexcept;
void trace_loading_file(const char* path,const CPU&) noexcept;
void trace_hid_delivery(const InputSnapshot&,const plat::PadState& delivered,uint64_t caller=0);
void loading_event(const char* path);
void test_file_delay(const char* path);
uint64_t seed();
uint64_t epoch();
uint32_t local_player();
uint32_t simulation_rate();
bool player_probe_ready();
TimingSnapshot timing();
PeerFrameTiming peer_timing(uint32_t player);  // frame time outside LAN wait, including pacing/render/scheduling
void flush_diagnostics() noexcept;
void set_tick_seed_filter(TickSeedFilter);
uint64_t guest_tick(uint64_t caller,uint64_t real_tick);
std::filesystem::path user_root(const std::filesystem::path& data);
bool start_lobby(LobbyRequest);
void cancel_lobby();
LobbyStatus lobby_status();
std::vector<std::string> local_addresses();
#else
inline void initialize() {}
inline bool active() { return false; }
inline bool connected(uint32_t) { return false; }
inline plat::PadState pad(uint32_t) { return {}; }
inline InputSnapshot input_snapshot(uint32_t) {return {};}
inline void next_frame() {}
inline void trace_framework_frame(uint64_t,uint64_t) {}
inline bool trace_phases_enabled() noexcept {return false;}
inline FrameworkObservation trace_phase_enter(const CPU&,CalcPhase) noexcept {return {};}
inline void trace_phase_leave(const CPU&,FrameworkObservation,CalcPhase,bool=false) noexcept {}
inline FrameworkObservation trace_framework_enter(const CPU&) noexcept {return {};}
inline void trace_framework_leave(const CPU&,FrameworkObservation,bool=false) noexcept {}
inline void trace_loading_file(const char*,const CPU&) noexcept {}
inline void trace_hid_delivery(const InputSnapshot&,const plat::PadState&,uint64_t=0) {}
inline void loading_event(const char*) {}
inline void test_file_delay(const char*) {}
inline uint64_t seed() { return 0; }
inline uint64_t epoch() { return 0; }
inline uint32_t local_player() { return 0; }
inline uint32_t simulation_rate() { return 60; }
inline bool player_probe_ready() { return false; }
inline TimingSnapshot timing() { return {}; }
inline PeerFrameTiming peer_timing(uint32_t) { return {}; }
inline void flush_diagnostics() noexcept {}
inline void set_tick_seed_filter(TickSeedFilter) {}
inline uint64_t guest_tick(uint64_t,uint64_t real_tick) { return real_tick; }
inline std::filesystem::path user_root(const std::filesystem::path& data) { return profiles::user_root(data); }
inline bool start_lobby(LobbyRequest) { return false; }
inline void cancel_lobby() {}
inline LobbyStatus lobby_status() { return {LobbyPhase::Failed,"Native LAN is available on Windows and Linux desktop builds."}; }
inline std::vector<std::string> local_addresses() { return {}; }
#endif
}
