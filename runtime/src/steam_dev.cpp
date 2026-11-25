#include "steam_dev.h"
#include <condition_variable>
#include <cwctype>
#include <mutex>
#include <thread>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace nx::steam_dev {
namespace {
bool valid_path(const std::filesystem::path& path) {
    if (!path.is_absolute()) return false;
    auto name = path.filename().wstring();
    for (auto& c : name) c = wchar_t(std::towlower(c));
    if (name != L"steam_api64.dll") return false;
#ifdef _WIN32
    // Explicit ordinary drive path only; no UNC/network/device namespace load.
    const auto drive = path.root_name().wstring();
    if (drive.size() != 2 || !std::iswalpha(drive[0]) || drive[1] != L':') return false;
#endif
    return true;
}
#ifdef _WIN32
struct ProcessLease { std::mutex mutex; bool active = false; };
ProcessLease& process_lease() {
    // Process-lifetime guard: safe even when another TU's static Bootstrap is
    // destroyed after this TU's ordinary static destruction would have run.
    static auto* lease = new ProcessLease;
    return *lease;
}
struct Module {
    HMODULE handle = nullptr;
    bool lease = false;
    ~Module() {
        // Never hold our loader/state mutex while executing DLL unload code.
        if (handle) FreeLibrary(handle);
        if (lease) { auto& process = process_lease(); std::lock_guard lock(process.mutex); process.active = false; }
    }
};
#endif
class NativeExporter final : public Exporter {
public:
    LoadResult load(const std::filesystem::path& path) override {
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) return {Status::LoadFailed, {}, GetLastError()};
        if (attributes & FILE_ATTRIBUTE_DIRECTORY) return {Status::LoadFailed, {}, ERROR_DIRECTORY};
        auto module = std::make_shared<Module>();
        {
            auto& process = process_lease();
            std::lock_guard lock(process.mutex);
            if (process.active) return {Status::AlreadyActive, {}};
            process.active = true; module->lease = true;
        }
        // Restrict dependencies to this explicitly selected SDK directory and
        // System32. No current-directory/PATH search or process-wide setting.
        module->handle = LoadLibraryExW(path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module->handle) return {Status::LoadFailed, {}, GetLastError()};
        Exports api;
        api.init = reinterpret_cast<InitFn>(GetProcAddress(module->handle, "SteamAPI_Init"));
        api.run_callbacks = reinterpret_cast<CallbacksFn>(GetProcAddress(module->handle, "SteamAPI_RunCallbacks"));
        api.shutdown = reinterpret_cast<ShutdownFn>(GetProcAddress(module->handle, "SteamAPI_Shutdown"));
        api.lifetime = std::move(module);
        const uint32_t missing = (!api.init ? MissingInit : 0u) |
            (!api.run_callbacks ? MissingCallbacks : 0u) | (!api.shutdown ? MissingShutdown : 0u);
        if (missing) return {Status::MissingExports, std::move(api), 0, missing};
        return {Status::Ready, std::move(api)};
#else
        (void)path;
        return {Status::UnsupportedPlatform, {}};
#endif
    }
};
}
const char* status_name(Status status) noexcept {
    switch (status) {
    case Status::Disabled: return "disabled"; case Status::InvalidPath: return "invalid-path";
    case Status::UnsupportedPlatform: return "unsupported-platform"; case Status::LoadFailed: return "load-failed";
    case Status::MissingExports: return "missing-exports"; case Status::AlreadyActive: return "already-active";
    case Status::InitFailed: return "init-failed"; case Status::InitException: return "init-exception";
    case Status::Ready: return "ready"; case Status::Busy: return "busy"; case Status::Stopped: return "stopped";
    case Status::Deferred: return "deferred"; case Status::CallbackFailed: return "callback-failed";
    case Status::ShutdownFailed: return "shutdown-failed";
    }
    return "unknown";
}
const char* status_hint(Status status, uint32_t error) noexcept {
    switch (status) {
    case Status::InvalidPath: return "Select an absolute local drive path ending in steam_api64.dll from the official Windows x64 SDK redistributable.";
    case Status::UnsupportedPlatform: return "This developer bootstrap currently supports Windows only.";
    case Status::LoadFailed:
        switch (error) {
        case 2: case 3: return "The selected SDK DLL or its directory is missing; restore the official SDK and choose a fresh developer fixture.";
        case 5: return "Windows denied access to the selected SDK DLL; check its file permissions and Windows protection history.";
        case 126: return "Windows could not load the selected DLL or one of its dependencies; use the complete official SDK x64 redistributable.";
        case 193: return "Windows rejected the DLL image; select the official Windows x64 steam_api64.dll, not an x86, corrupt or non-Windows file.";
        case 267: return "The selected SDK DLL path refers to a directory; select the official redistributable file.";
        default: return "Windows could not load the selected SDK DLL; inspect loader_error and use the complete official Windows x64 SDK redistributable.";
        }
    case Status::MissingExports: return "The selected DLL lacks required lifecycle exports; replace it with the official matching SDK redistributable. missing_exports bits: Init=1, RunCallbacks=2, Shutdown=4.";
    case Status::AlreadyActive: return "Another bootstrap owns the Steam API module in this process; stop it before initializing another instance.";
    case Status::InitFailed: return "SteamAPI_Init returned false without a specific cause. Check Steam is running under the same Windows user and elevation, the isolated working directory/AppID480 file, account access to that AppID and app configuration. A process check alone does not prove these conditions.";
    case Status::InitException: return "SteamAPI_Init raised an exception; preserve the log and check the official SDK DLL and client installation.";
    case Status::CallbackFailed: return "The Steam callback pump raised an exception; this bootstrap stopped and attempted shutdown.";
    case Status::ShutdownFailed: return "SteamAPI_Shutdown raised an exception; completed teardown is unverified.";
    case Status::Ready: return "Only local Steam API initialization succeeded; Remote Play sessions, independent guest controls and P2P are unverified.";
    default: return "Inspect the lifecycle status and counters; no multiplayer capability is inferred from this state.";
    }
}
struct Bootstrap::State {
    enum class Phase { Fresh, Initializing, Ready, Pumping, Stopping, Stopped, Failed };
    Config config;
    std::shared_ptr<Exporter> exporter;
    std::mutex mutex;
    std::condition_variable changed;
    Phase phase = Phase::Fresh;
    Status terminal = Status::Stopped;
    bool stop_requested = false, initialized = false;
    std::thread::id call_thread;
    Exports api;
    Snapshot stats;
    State(Config value, std::shared_ptr<Exporter> loader) : config(std::move(value)), exporter(std::move(loader)) {}
};
Bootstrap::Bootstrap(Config config, std::shared_ptr<Exporter> exporter)
    : state_(std::make_shared<State>(std::move(config), std::move(exporter))) {}
Bootstrap::~Bootstrap() { shutdown(); }
Status Bootstrap::teardown(const std::shared_ptr<State>& s, Status result, bool failed) noexcept {
    Exports api;
    bool initialized;
    {
        std::lock_guard lock(s->mutex);
        s->phase = State::Phase::Stopping;
        s->call_thread = std::this_thread::get_id();
        initialized = s->initialized; s->initialized = false;
        if (initialized) ++s->stats.shutdown_calls;
        api = std::move(s->api);
    }
    bool shutdown_completed = false;
    if (initialized) {
        try { api.shutdown(); shutdown_completed = true; } catch (...) { result = Status::ShutdownFailed; failed = true; }
    }
    api.lifetime.reset(); // No lock: module deleters may reenter shutdown().
    {
        std::lock_guard lock(s->mutex);
        s->terminal = result;
        if (shutdown_completed) ++s->stats.shutdown_completed;
        s->phase = failed ? State::Phase::Failed : State::Phase::Stopped;
        s->call_thread = {};
    }
    s->changed.notify_all();
    return result;
}
Status Bootstrap::initialize() {
    const auto s = state_;
    if (!s->config.enabled) return Status::Disabled;
    {
        std::lock_guard lock(s->mutex);
        switch (s->phase) {
        case State::Phase::Ready: case State::Phase::Pumping: return s->stop_requested ? Status::Busy : Status::Ready;
        case State::Phase::Initializing: case State::Phase::Stopping: return Status::Busy;
        case State::Phase::Stopped: case State::Phase::Failed: return s->terminal;
        case State::Phase::Fresh: break;
        }
        s->phase = State::Phase::Initializing;
        s->call_thread = std::this_thread::get_id();
    }
    LoadResult loaded;
    try {
        if (!valid_path(s->config.absolute_library)) return teardown(s, Status::InvalidPath, true);
        if (s->exporter) loaded = s->exporter->load(s->config.absolute_library);
        else { NativeExporter exporter; loaded = exporter.load(s->config.absolute_library); }
    } catch (...) { return teardown(s, Status::LoadFailed, true); }
    {
        std::lock_guard lock(s->mutex);
        s->stats.loader_error = loaded.loader_error;
        s->stats.missing_exports = loaded.missing_exports;
        s->api = std::move(loaded.api);
    }
    if (loaded.status != Status::Ready) return teardown(s, loaded.status, true);
    if (!s->api.init || !s->api.run_callbacks || !s->api.shutdown) {
        {
            std::lock_guard lock(s->mutex);
            s->stats.missing_exports |= (!s->api.init ? MissingInit : 0u) |
                (!s->api.run_callbacks ? MissingCallbacks : 0u) | (!s->api.shutdown ? MissingShutdown : 0u);
        }
        return teardown(s, Status::MissingExports, true);
    }
    bool stop;
    { std::lock_guard lock(s->mutex); stop = s->stop_requested; }
    if (stop) return teardown(s, Status::Stopped, false);
    { std::lock_guard lock(s->mutex); s->stats.init_attempted = true; }
    bool initialized;
    try { initialized = s->api.init(); }
    catch (...) { return teardown(s, Status::InitException, true); }
    if (!initialized) return teardown(s, Status::InitFailed, true);
    {
        std::lock_guard lock(s->mutex);
        s->initialized = true;
        s->stats.initialized_ever = true;
        stop = s->stop_requested;
        if (!stop) { s->phase = State::Phase::Ready; s->call_thread = {}; }
    }
    s->changed.notify_all();
    return stop ? teardown(s, Status::Stopped, false) : Status::Ready;
}
Status Bootstrap::pump() {
    const auto s = state_;
    if (!s->config.enabled) return Status::Disabled;
    {
        std::lock_guard lock(s->mutex);
        if (s->phase == State::Phase::Initializing || s->phase == State::Phase::Pumping || s->phase == State::Phase::Stopping) return Status::Busy;
        if (s->phase != State::Phase::Ready || s->stop_requested) return s->terminal;
        s->phase = State::Phase::Pumping; s->call_thread = std::this_thread::get_id();
        ++s->stats.callback_calls;
    }
    bool exception = false;
    try { s->api.run_callbacks(); } catch (...) { exception = true; }
    bool stop;
    {
        std::lock_guard lock(s->mutex);
        if (exception) { s->stop_requested = true; ++s->stats.callback_failures; }
        else ++s->stats.callback_completed;
        stop = s->stop_requested;
        if (!stop) { s->phase = State::Phase::Ready; s->call_thread = {}; }
    }
    s->changed.notify_all();
    if (stop) return teardown(s, exception ? Status::CallbackFailed : Status::Stopped, exception);
    return Status::Ready;
}
Status Bootstrap::shutdown() noexcept {
    const auto s = state_;
    if (!s->config.enabled) return Status::Disabled;
    bool teardown_here = false;
    {
        std::unique_lock lock(s->mutex);
        s->stop_requested = true;
        if (s->phase == State::Phase::Stopped || s->phase == State::Phase::Failed) return s->terminal;
        if (s->phase == State::Phase::Fresh) { s->phase = State::Phase::Stopped; return Status::Stopped; }
        if (s->phase == State::Phase::Ready) {
            s->phase = State::Phase::Stopping; s->call_thread = std::this_thread::get_id(); teardown_here = true;
        } else {
            if (s->call_thread == std::this_thread::get_id()) return Status::Deferred;
            s->changed.wait(lock, [&] { return s->phase == State::Phase::Stopped || s->phase == State::Phase::Failed; });
            return s->terminal;
        }
    }
    return teardown_here ? teardown(s, Status::Stopped, false) : Status::Stopped;
}
bool Bootstrap::ready() const noexcept {
    const auto s = state_;
    std::lock_guard lock(s->mutex);
    return !s->stop_requested && (s->phase == State::Phase::Ready || s->phase == State::Phase::Pumping);
}
Snapshot Bootstrap::snapshot() const noexcept {
    const auto s = state_;
    std::lock_guard lock(s->mutex);
    auto result = s->stats;
    if (!s->config.enabled) result.status = Status::Disabled;
    else switch (s->phase) {
    case State::Phase::Fresh: result.status = Status::Stopped; break;
    case State::Phase::Initializing: case State::Phase::Pumping:
    case State::Phase::Stopping: result.status = Status::Busy; break;
    case State::Phase::Ready: result.status = s->stop_requested ? Status::Busy : Status::Ready; break;
    case State::Phase::Stopped: case State::Phase::Failed: result.status = s->terminal; break;
    }
    return result;
}
} // namespace nx::steam_dev
