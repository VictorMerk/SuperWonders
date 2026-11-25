#include "steam_dev.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <fstream>
#include <future>
#include <cstring>
#include <stdexcept>
#include <thread>

using namespace nx::steam_dev;
namespace {
void check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "steam dev mock: %s\n", message); std::exit(1); }
}
struct Fixture {
    std::atomic<unsigned> loads{0}, inits{0}, pumps{0}, shutdowns{0}, unloads{0}, active{0};
    bool init_ok = true, load_ok = true, init_export = true, pump_export = true, shutdown_export = true;
    uint32_t loader_error = 0;
    std::function<void()> on_init, on_pump, on_shutdown, on_unload;
};
Fixture* current;
struct Active {
    Fixture& fixture;
    explicit Active(Fixture& value) : fixture(value) { ++fixture.active; }
    ~Active() { --fixture.active; }
};
bool fake_init() { auto& f = *current; Active active(f); ++f.inits; if (f.on_init) f.on_init(); return f.init_ok; }
void fake_pump() { auto& f = *current; Active active(f); ++f.pumps; if (f.on_pump) f.on_pump(); }
void fake_shutdown() { auto& f = *current; Active active(f); ++f.shutdowns; if (f.on_shutdown) f.on_shutdown(); }
class FakeExporter final : public Exporter {
public:
    Fixture& fixture;
    explicit FakeExporter(Fixture& value) : fixture(value) { current = &value; }
    LoadResult load(const std::filesystem::path&) override {
        ++fixture.loads;
        if (!fixture.load_ok) return {Status::LoadFailed, {}, fixture.loader_error};
        Exports api;
        api.init = fixture.init_export ? fake_init : nullptr;
        api.run_callbacks = fixture.pump_export ? fake_pump : nullptr;
        api.shutdown = fixture.shutdown_export ? fake_shutdown : nullptr;
        api.lifetime = std::shared_ptr<void>(new int(1), [f = &fixture](void* token) {
            check(f->active == 0, "module lifetime survives every API call");
            ++f->unloads; if (f->on_unload) f->on_unload(); delete static_cast<int*>(token);
        });
        return {Status::Ready, std::move(api)};
    }
};
Config enabled() {
#ifdef _WIN32
    return {true, L"C:/explicit-official-sdk-dev/steam_api64.dll"};
#else
    return {true, "/explicit-official-sdk-dev/steam_api64.dll"};
#endif
}
void entered(std::future<void>& future) {
    check(future.wait_for(std::chrono::seconds(2)) == std::future_status::ready, "mock entered API call within deadline");
}
}
int main() {
    {
        Fixture f; auto loader = std::make_shared<FakeExporter>(f);
        Bootstrap bootstrap({}, loader);
        check(bootstrap.initialize() == Status::Disabled && bootstrap.pump() == Status::Disabled &&
              bootstrap.shutdown() == Status::Disabled && !bootstrap.ready(), "default-off never loads or calls Steam");
        check(f.loads == 0 && f.inits == 0 && f.pumps == 0 && f.shutdowns == 0, "disabled has no exporter/API side effects");
        const auto stats = bootstrap.snapshot();
        check(stats.status == Status::Disabled && !stats.init_attempted && !stats.initialized_ever && stats.callback_calls == 0 && stats.shutdown_calls == 0, "disabled snapshot has no API activity");
        check(stats.loader_error == 0 && stats.missing_exports == 0, "disabled snapshot has no fabricated loader diagnostics");
        Bootstrap default_loader;
        check(default_loader.initialize() == Status::Disabled, "native loader is unreachable when disabled");
    }
    {
        Fixture f; auto loader = std::make_shared<FakeExporter>(f);
        Bootstrap relative({true, "steam_api64.dll"}, loader);
        check(relative.initialize() == Status::InvalidPath && f.loads == 0, "relative DLL path rejected before exporter");
        auto config = enabled(); config.absolute_library.replace_filename("another.dll");
        Bootstrap wrong_name(config, loader);
        check(wrong_name.initialize() == Status::InvalidPath && f.loads == 0, "unexpected library name rejected");
#ifdef _WIN32
        Bootstrap unc({true, L"\\\\server\\share\\steam_api64.dll"}, loader);
        check(unc.initialize() == Status::InvalidPath && f.loads == 0, "UNC paths cannot trigger network DLL loads");
#endif
    }
    {
        Fixture f; f.load_ok = false; f.loader_error = 193;
        Bootstrap bootstrap(enabled(), std::make_shared<FakeExporter>(f));
        check(bootstrap.initialize() == Status::LoadFailed && bootstrap.initialize() == Status::LoadFailed &&
              f.loads == 1 && f.inits == 0 && f.shutdowns == 0, "unavailable DLL honest terminal failure, no API calls");
        check(bootstrap.snapshot().loader_error == 193 && bootstrap.snapshot().missing_exports == 0,
              "native diagnostic survives terminal failure and repeat initialize");
        check(std::strstr(status_hint(Status::LoadFailed, 193), "x64") != nullptr &&
              std::strstr(status_hint(Status::InitFailed), "without a specific cause") != nullptr,
              "bad-image error actionable and legacy Init false is not misdiagnosed");
    }
    for (int missing = 0; missing < 3; ++missing) {
        Fixture f;
        if (missing == 0) f.init_export = false;
        if (missing == 1) f.pump_export = false;
        if (missing == 2) f.shutdown_export = false;
        Bootstrap bootstrap(enabled(), std::make_shared<FakeExporter>(f));
        check(bootstrap.initialize() == Status::MissingExports && f.inits == 0 && f.shutdowns == 0 && f.unloads == 1,
              "all required exports checked before Init; missing export releases module");
        check(bootstrap.snapshot().missing_exports == (1u << missing) && bootstrap.snapshot().loader_error == 0,
              "exact missing export recorded even when injected exporter omitted mask");
    }
#ifdef _WIN32
    {
        // Exercise the production Windows loader, never a Steam DLL: missing
        // file and deliberately invalid image must fail before any API call.
        const auto folder = std::filesystem::temp_directory_path() /
            ("wonder-steam-loader-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(std::filesystem::create_directory(folder), "create isolated native loader fixture");
        const auto library = folder / "steam_api64.dll";
        {
            Bootstrap missing({true, library});
            check(missing.initialize() == Status::LoadFailed, "production loader rejects absent library");
            const auto stats = missing.snapshot();
            check(stats.loader_error == 2 && !stats.init_attempted && !stats.initialized_ever && stats.shutdown_calls == 0,
                  "production missing-file Windows error retained without API calls");
        }
        { std::ofstream invalid(library, std::ios::binary); invalid << "deliberately not a PE DLL"; }
        for (int attempt = 0; attempt < 2; ++attempt) {
            Bootstrap invalid({true, library});
            check(invalid.initialize() == Status::LoadFailed, "production loader rejects invalid image and releases process lease");
            const auto stats = invalid.snapshot();
            check(stats.loader_error == 193 && !stats.init_attempted && stats.callback_calls == 0 && stats.shutdown_calls == 0,
                  "production invalid-image Windows error retained without Steam API calls");
        }
        check(std::filesystem::remove(library) && std::filesystem::remove(folder), "remove exact owned native loader fixture");
    }
#endif
    {
        Fixture f; f.init_ok = false;
        Bootstrap bootstrap(enabled(), std::make_shared<FakeExporter>(f));
        check(bootstrap.initialize() == Status::InitFailed && bootstrap.initialize() == Status::InitFailed &&
              bootstrap.pump() == Status::InitFailed && !bootstrap.ready(), "actual Init false remains honest failure");
        check(f.inits == 1 && f.shutdowns == 0 && f.unloads == 1, "failed Init never fabricates initialized teardown");
        const auto stats = bootstrap.snapshot();
        check(stats.status == Status::InitFailed && stats.init_attempted && !stats.initialized_ever && stats.shutdown_calls == 0, "failed initialization snapshot is honest");
    }
    {
        Fixture f; f.on_init = [] { throw std::runtime_error("mock Init failure"); };
        Bootstrap bootstrap(enabled(), std::make_shared<FakeExporter>(f));
        check(bootstrap.initialize() == Status::InitException && !bootstrap.ready() && f.inits == 1 &&
              f.shutdowns == 0 && f.unloads == 1, "Init exception does not fabricate success or leave module alive");
    }
    {
        Fixture f;
        Bootstrap bootstrap(enabled(), std::make_shared<FakeExporter>(f));
        check(bootstrap.initialize() == Status::Ready && bootstrap.initialize() == Status::Ready && bootstrap.ready() &&
              f.loads == 1 && f.inits == 1, "single successful Init");
        check(bootstrap.pump() == Status::Ready && bootstrap.pump() == Status::Ready && f.pumps == 2, "pump forwards real injected function");
        check(bootstrap.shutdown() == Status::Stopped && bootstrap.shutdown() == Status::Stopped && !bootstrap.ready() &&
              f.shutdowns == 1 && f.unloads == 1 && bootstrap.initialize() == Status::Stopped, "exactly-once shutdown and terminal stopped lifecycle");
        const auto stats = bootstrap.snapshot();
        check(stats.status == Status::Stopped && stats.init_attempted && stats.initialized_ever && stats.callback_calls == 2 && stats.callback_completed == 2 && stats.callback_failures == 0 && stats.shutdown_calls == 1 && stats.shutdown_completed == 1, "snapshot counts actual API calls rather than lifecycle queries");
    }
    {
        Fixture f;
        Bootstrap bootstrap(enabled(), std::make_shared<FakeExporter>(f));
        check(bootstrap.initialize() == Status::Ready, "callback fixture initialized");
        f.on_pump = [&] {
            check(bootstrap.pump() == Status::Busy && bootstrap.initialize() == Status::Ready, "callback reentrancy never recursively pumps/initializes");
            check(bootstrap.shutdown() == Status::Deferred && !bootstrap.ready(), "callback requests deferred shutdown without deadlock");
            check(f.shutdowns == 0 && f.unloads == 0, "teardown waits until callback returns");
        };
        f.on_shutdown = [&] { check(bootstrap.shutdown() == Status::Deferred, "shutdown callback can reenter safely"); };
        f.on_unload = [&] { check(bootstrap.shutdown() == Status::Deferred, "module deleter runs outside state mutex"); };
        check(bootstrap.pump() == Status::Stopped && f.pumps == 1 && f.shutdowns == 1 && f.unloads == 1, "last pump performs deferred teardown exactly once");
    }
    {
        Fixture f;
        Bootstrap bootstrap(enabled(), std::make_shared<FakeExporter>(f));
        check(bootstrap.initialize() == Status::Ready, "exception fixture initialized");
        f.on_pump = [] { throw std::runtime_error("mock callback failure"); };
        check(bootstrap.pump() == Status::CallbackFailed && !bootstrap.ready() && f.shutdowns == 1 && f.unloads == 1,
              "callback exception unwinds active call before safe teardown");
        const auto stats = bootstrap.snapshot();
        check(stats.status == Status::CallbackFailed && stats.callback_calls == 1 && stats.callback_completed == 0 && stats.callback_failures == 1 && stats.shutdown_completed == 1, "failed callback is not counted completed");
    }
    {
        Fixture f;
        Bootstrap bootstrap(enabled(), std::make_shared<FakeExporter>(f));
        check(bootstrap.initialize() == Status::Ready, "shutdown exception fixture initialized");
        f.on_shutdown = [] { throw std::runtime_error("mock Shutdown failure"); };
        check(bootstrap.shutdown() == Status::ShutdownFailed && bootstrap.shutdown() == Status::ShutdownFailed &&
              !bootstrap.ready() && f.shutdowns == 1 && f.unloads == 1, "shutdown exception terminal, module released and no second API teardown");
        const auto stats = bootstrap.snapshot();
        check(stats.status == Status::ShutdownFailed && stats.shutdown_calls == 1 && stats.shutdown_completed == 0, "failed shutdown remains distinguishable from completed teardown");
    }
    {
        Fixture f;
        Bootstrap bootstrap(enabled(), std::make_shared<FakeExporter>(f));
        check(bootstrap.initialize() == Status::Ready, "concurrent fixture initialized");
        std::promise<void> start, release;
        auto started = start.get_future(); auto gate = release.get_future().share();
        f.on_pump = [&] { start.set_value(); gate.wait(); };
        Status pumped = Status::Busy, stopped = Status::Busy, stopped_again = Status::Busy;
        std::thread pump([&] { pumped = bootstrap.pump(); }); entered(started);
        check(bootstrap.pump() == Status::Busy, "concurrent pump is serialized");
        std::atomic<bool> stop_finished{false};
        std::thread stop([&] { stopped = bootstrap.shutdown(); stop_finished = true; });
        std::thread stop_again([&] { stopped_again = bootstrap.shutdown(); });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (bootstrap.ready() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
        check(!bootstrap.ready() && !stop_finished && f.shutdowns == 0 && f.unloads == 0, "external stop waits without unloading executing callbacks");
        const auto in_flight = bootstrap.snapshot();
        check(in_flight.status == Status::Busy && in_flight.callback_calls == 1 && in_flight.callback_completed == 0 && in_flight.shutdown_calls == 0, "concurrent snapshot observes in-flight callback without assuming completion");
        release.set_value(); pump.join(); stop.join(); stop_again.join();
        check(pumped == Status::Stopped && stopped == Status::Stopped && stopped_again == Status::Stopped && f.pumps == 1 && f.shutdowns == 1 && f.unloads == 1,
              "concurrent stop and pump safely converge on exactly one teardown");
    }
    {
        Fixture f; std::promise<void> start, release;
        auto started = start.get_future(); auto gate = release.get_future().share();
        f.on_init = [&] { start.set_value(); gate.wait(); };
        Bootstrap bootstrap(enabled(), std::make_shared<FakeExporter>(f));
        Status initialized = Status::Busy, stopped = Status::Busy;
        std::thread init([&] { initialized = bootstrap.initialize(); }); entered(started);
        check(bootstrap.initialize() == Status::Busy && f.inits == 1, "concurrent initialize cannot call Init twice");
        std::promise<void> stopping; auto stop_started = stopping.get_future();
        std::thread stop([&] { stopping.set_value(); stopped = bootstrap.shutdown(); }); entered(stop_started);
        release.set_value(); init.join(); stop.join();
        check((initialized == Status::Ready || initialized == Status::Stopped) && stopped == Status::Stopped &&
              f.inits == 1 && f.shutdowns == 1 && f.unloads == 1 && !bootstrap.ready(), "stop during Init retains module and tears down after real success");
    }
    {
        Fixture f;
        Bootstrap bootstrap(enabled(), std::make_shared<FakeExporter>(f));
        check(bootstrap.shutdown() == Status::Stopped && bootstrap.initialize() == Status::Stopped && f.loads == 0,
              "early process stop cannot later initialize Steam");
    }
    std::puts("Steam bootstrap CPU lifecycle and loader rejection tests passed; no Steam DLL/client/API calls performed");
}
