#include "steam_dev_runtime.h"
#include "steam_dev.h"
#include "netplay_game.h"
#include "native_lan_runtime.h"
#include <nx/game.h>
#include <nx/runtime.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace nx::steam_dev {
namespace {
std::atomic_flag stop_started = ATOMIC_FLAG_INIT;
std::atomic<Bootstrap*> current{nullptr}; // Process lifetime, published before guest threads.
void write_stats(const Bootstrap& api, const char* checkpoint) {
    const auto s = api.snapshot();
    const char* requested = std::getenv("NX_STEAM_STATS");
    const auto path = requested && *requested
        ? std::filesystem::path(reinterpret_cast<const char8_t*>(requested))
        : std::filesystem::path("steam-dev-stats.json");
    std::ofstream out(path, std::ios::trunc);
    if (!out) { log("Steam developer statistics could not be written"); return; }
    out << "{\"schema\":1,\"source\":\"steam_dev_bootstrap\",\"app_id\":480,"
        << "\"checkpoint\":\"" << checkpoint << "\",\"status\":\"" << status_name(s.status) << "\","
        << "\"init_attempted\":" << (s.init_attempted ? "true" : "false")
        << ",\"initialized_ever\":" << (s.initialized_ever ? "true" : "false")
        << ",\"callback_calls\":" << s.callback_calls
        << ",\"callback_completed\":" << s.callback_completed
        << ",\"callback_failures\":" << s.callback_failures
        << ",\"shutdown_calls\":" << s.shutdown_calls
        << ",\"shutdown_completed\":" << s.shutdown_completed
        << ",\"loader_error\":" << s.loader_error
        << ",\"missing_exports\":" << s.missing_exports
        << ",\"remote_play_together_verified\":false,\"p2p_transport_verified\":false,"
        << "\"scope\":\"Local Steam API lifecycle only; no room, player, packet, RTT, FPS or gameplay certification\"}\n";
    if (!out) log("Steam developer statistics write failed");
}
void stop() {
    if (stop_started.test_and_set()) return;
    if(auto* api=current.load(std::memory_order_acquire)) {
        const auto status=api->shutdown();
        write_stats(*api, "shutdown");
        log("Steam developer bootstrap: shutdown %s",status_name(status));
    }
}
bool set(const char* key) {const auto* value=std::getenv(key);return value&&*value&&std::strcmp(value,"0");}
}
bool active() noexcept {return current.load(std::memory_order_acquire)!=nullptr;}
void shutdown_current() {stop();}
void start_from_environment() {
    const char* flag=std::getenv("NX_STEAM_DEV");
    if(!flag||!std::strcmp(flag,"0"))return;
    if(std::strcmp(flag,"1"))fatal("NX_STEAM_DEV must be 0 or 1");
#ifndef _WIN32
    fatal("The Steam developer bootstrap currently supports Windows only");
#else
    if(std::strcmp(game::kInfo.id,"wonder"))fatal("Steam developer mode currently supports Wonder only");
    if(lan::active()||native_lan::active()||set("NX_LAN_MODE")||set("NX_LAN_LOBBY_AUTOSTART")||set("NX_NATIVE_LAN"))
        fatal("Steam streaming development uses one local co-op instance; clear both LAN modes");
    if(!set("NX_NO_LAUNCHER"))fatal("Use the isolated Steam developer launcher for this bootstrap");
    if(active())fatal("Steam developer bootstrap may only start once");
    std::wstring executable(32768,L'\0');
    const auto length=GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));
    if(!length||length>=executable.size())fatal("Could not locate the Steam developer runtime");
    executable.resize(length);
    const auto directory=std::filesystem::path(executable).parent_path();
    std::error_code path_error;
    if(!std::filesystem::equivalent(directory,std::filesystem::current_path(),path_error)||path_error)
        fatal("Steam developer mode requires its isolated executable directory as the working directory");
    std::ifstream appid(directory/"steam_appid.txt");std::string id,extra;
    if(!(appid>>id)||id!="480"||(appid>>extra))fatal("Steam developer mode requires the isolated Spacewar AppID480 fixture");
    const char* library=std::getenv("NX_STEAM_API_LIBRARY");
    if(!library||!*library)fatal("Provide the official SDK steam_api64.dll through NX_STEAM_API_LIBRARY");
    auto api=std::make_unique<Bootstrap>(Config{true,std::filesystem::path(reinterpret_cast<const char8_t*>(library))});
    const auto status=api->initialize();
    write_stats(*api, "initialization");
    if(status!=Status::Ready) {
        const auto diagnostics=api->snapshot();
        fatal("Steam developer API initialization failed: %s (loader_error=%u, missing_exports=%u). %s",
            status_name(status), diagnostics.loader_error, diagnostics.missing_exports,
            status_hint(status, diagnostics.loader_error));
    }
    current.store(api.release(),std::memory_order_release);
    std::atexit(stop);
    log("Steam developer bootstrap ready for AppID480; Remote Play Together, overlay, lobbies and P2P are not certified by this result");
#endif
}
void pump_current() {
    auto* api=current.load(std::memory_order_acquire);
    if(!api)return;
    const auto status=api->pump();
    if(status!=Status::Ready&&status!=Status::Busy&&status!=Status::Stopped&&status!=Status::Deferred&&status!=Status::Disabled) {
        write_stats(*api, "callback-failure");
        fatal("Steam developer callback pump stopped: %s. %s",status_name(status),status_hint(status));
    }
}
}
