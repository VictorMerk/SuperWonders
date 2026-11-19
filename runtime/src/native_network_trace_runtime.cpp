#include "native_network_trace.h"
#include <nx/gamelib.h>
#include <nx/game.h>
#include <nx/runtime.h>
#include <cstdlib>
#include <cstring>

namespace nx::native_net {
namespace {
Census* runtime_census = nullptr; // Startup publication, never retired during guest execution.
void output(const Event& e) {
    log("native-net experimental sdk-census: op=%s call=%llu caller=%s+0x%llx fields=%lld,%lld,%lld,%lld result_valid=%d result=%lld error=%d exception=%d duration_ns=%llu",
        e.operation, (unsigned long long)e.call, e.caller_module, (unsigned long long)e.caller_offset,
        (long long)e.fields[0], (long long)e.fields[1], (long long)e.fields[2], (long long)e.fields[3],
        int(e.has_result), (long long)e.result, int(e.error), int(e.exception), (unsigned long long)e.duration_ns);
}
}
void init() {
    if(std::strcmp(game::kInfo.id,"wonder")) return;
    const char* enabled = std::getenv("NX_NATIVE_NET_TRACE");
    if (!enabled || std::strcmp(enabled, "1")) return;
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    const auto& game = game_lib();
    if (!game.sdk || !game.main) return;
    const auto* sdk = module_at(game.sdk->base);
    if (!sdk) return;
    // Process lifetime: no table restoration while guest threads may still run.
    auto* census = new Census;
    const size_t count = census->install(true, *sdk->info, sdk->table,
        {game.main->base, game.main->text_size, game.main->text},
        {sdk->base, sdk->info->text_size, sdk->info->text}, output);
    size_t main_count=0;
    if(const auto* main=module_at(game.main->base)) {
        // Product120 code prefixes independently matched the original NSO.
        // Observe factory/construction/preparation even if SDK setup fails first.
        constexpr GuardedSite sites[]={
            {0x418634,0xc23229310081f88full,"wonder.NativeLanFactory",false},
            {0x54fe54,0x207d9a1b879801cfull,"wonder.NetworkStateConstructor",true},
            {0x1db7bec,0xd58385cbeedbc991ull,"wonder.NetworkPrepare",true},
        };
        main_count=census->install_main(true,*main->info,main->table,
            {reinterpret_cast<const uint8_t*>(main->base),size_t(main->info->text_size)},
            "ff773e90972d544eb79406eaa65396d53c43efb9000000000000000000000000",sites,output);
    }
    log("native-net experimental sdk-census: installed=%zu guarded_main=%zu max_events=256; original behavior retained; direct HLE calls excluded", count,main_count);
    if (!count && !main_count) delete census;
    else {
        runtime_census = census;
        std::atexit([] {
            // Atomic, non-transactional snapshot; guest threads may still finish
            // a call. At most 13 summaries, independent of event sampling cap.
            for(const auto& counter:diagnostics()) if(counter.operation)
                log("native-net cumulative: op=%s calls=%llu errors=%llu exceptions=%llu total_ns=%llu peak_ns=%llu",
                    counter.operation,(unsigned long long)counter.calls,
                    (unsigned long long)counter.errors,(unsigned long long)counter.exceptions,
                    (unsigned long long)counter.total_ns,(unsigned long long)counter.peak_ns);
        });
    }
}
std::array<Counters, kMaxHooks> diagnostics() noexcept {
    return runtime_census ? runtime_census->snapshot() : std::array<Counters, kMaxHooks>{};
}
} // namespace nx::native_net
