#pragma once
#include <filesystem>
#include <cstdint>
#include <memory>

namespace nx::steam_dev {

// Minimal signatures documented by Valve, not copied SDK declarations/code:
// https://partner.steamgames.com/doc/api/steam_api
#ifdef _WIN32
using InitFn = bool (__cdecl*)();
using CallbacksFn = void (__cdecl*)();
using ShutdownFn = void (__cdecl*)();
#else
using InitFn = bool (*)();
using CallbacksFn = void (*)();
using ShutdownFn = void (*)();
#endif
enum class Status {
    Disabled, InvalidPath, UnsupportedPlatform, LoadFailed, MissingExports,
    AlreadyActive, InitFailed, InitException, Ready, Busy, Stopped, Deferred,
    CallbackFailed, ShutdownFailed,
};
const char* status_name(Status status) noexcept;
// Actionable checks, not a diagnosis of Steam account/client state. In
// particular, the legacy boolean Init export cannot distinguish its failures.
const char* status_hint(Status status, uint32_t loader_error = 0) noexcept;
enum MissingExport : uint32_t { MissingInit = 1, MissingCallbacks = 2, MissingShutdown = 4 };
struct Exports {
    InitFn init = nullptr;
    CallbacksFn run_callbacks = nullptr;
    ShutdownFn shutdown = nullptr;
    // Owns the loaded module through every API call and deferred shutdown.
    std::shared_ptr<void> lifetime;
};
struct LoadResult {
    Status status = Status::LoadFailed;
    Exports api;
    uint32_t loader_error = 0; // Native Windows error, zero when unavailable.
    uint32_t missing_exports = 0; // MissingExport bits; no speculative API calls.
};
class Exporter {
public:
    virtual ~Exporter() = default;
    virtual LoadResult load(const std::filesystem::path& absolute_library) = 0;
};
struct Config {
    bool enabled = false;
    std::filesystem::path absolute_library;
};
// Local API lifecycle counters only; these do not measure network packets,
// remote players, delivery, round-trip latency or Remote Play capability.
struct Snapshot {
    Status status = Status::Disabled;
    bool init_attempted = false, initialized_ever = false;
    uint64_t callback_calls = 0, callback_completed = 0, callback_failures = 0;
    uint64_t shutdown_calls = 0, shutdown_completed = 0;
    uint32_t loader_error = 0, missing_exports = 0;
};
// Explicit, optional bootstrap only: successful initialization proves no overlay,
// Remote Play Together, friend, lobby, P2P or gameplay capability. Root launch
// code owns NX_STEAM_DEV and the isolated development appid480 working folder.
// This class never reads/writes appid files, changes environment/cwd, restarts
// Steam, registers callbacks, reads accounts/friends or sends invitations.
class Bootstrap {
public:
    explicit Bootstrap(Config config = {}, std::shared_ptr<Exporter> exporter = {});
    ~Bootstrap();
    Bootstrap(const Bootstrap&) = delete;
    Bootstrap& operator=(const Bootstrap&) = delete;
    // At most one Init call per object. Failure/stopped states are terminal.
    Status initialize();
    // Serial callback pump. Reentrant/concurrent pump returns Busy; callbacks
    // execute with no state mutex held. Root should pump from one chosen thread.
    Status pump();
    // External stop waits for in-flight Init/pump/teardown. Called inside one of
    // those callbacks, it requests stop and returns Deferred; last call tears down.
    // Shutdown runs exactly once iff Init returned true; module unload follows.
    Status shutdown() noexcept;
    bool ready() const noexcept;
    Snapshot snapshot() const noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
    static Status teardown(const std::shared_ptr<State>& state, Status result, bool failed) noexcept;
};
} // namespace nx::steam_dev
