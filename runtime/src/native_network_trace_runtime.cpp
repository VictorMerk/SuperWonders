#include "native_network_trace.h"
#include <nx/gamelib.h>
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
    log("native-net experimental sdk-census: installed=%zu max_events=256; original SDK behavior retained; direct HLE calls excluded", count);
    if (!count) delete census;
    else runtime_census = census;
}
std::array<Counters, kMaxHooks> diagnostics() noexcept {
    return runtime_census ? runtime_census->snapshot() : std::array<Counters, kMaxHooks>{};
}
} // namespace nx::native_net
