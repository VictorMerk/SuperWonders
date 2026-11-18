// Synchronize at the end of Wonder 1.0.0's full framework frame. A queued NVN
// present is earlier: the framework may still finish work after submitting it.
#include <nx/runtime.h>
#include <nx/gamelib.h>
#include <nx/module.h>
#include "netplay_game.h"
#include "play.h"
#include "lan_seed.h"
#include <atomic>

namespace nx::wonder {
namespace {
constexpr uint64_t offset = 0xe15ec;
constexpr uint32_t expected[] = {0xd10143ff,0xa9027bfd,0xa90357f6,0xa9044ff4,
                               0x910083fd,0x3949d009,0xaa0003f3,0x7100013f};
Fn original = nullptr;
uint64_t seed_tick(uint64_t caller,uint64_t real_tick) {
    const auto* main=game_lib().main;
    if (!main || caller<main->base) return real_tick;
    const auto* site=lan_seed::find(caller-main->base);
    if (!site) return real_tick;
    std::array<uint32_t,8> words;
    for (size_t i=0;i<words.size();++i) words[i]=ld<uint32_t>(main->base+site->call+i*4);
    if (!lan_seed::verified(*site,words)) fatal("Native LAN: RNG seed code does not match verified Wonder 1.0.0");
    static std::atomic<uint32_t> logged{0};
    const uint32_t bit=1u<<unsigned(site-lan_seed::sites);
    if (!(logged.fetch_or(bit)&bit))
        log("lan: shared-session RNG initialization at main+%llx (ordinary clocks unchanged)",(unsigned long long)site->call);
    return lan_seed::value(lan::seed(),*site);
}
// Register before guest module constructors run; after_init is too late for
// static random generators. Atomic filter storage has constant initialization.
const bool seed_filter_registered=[] { lan::set_tick_seed_filter(seed_tick); return true; }();
void framework_frame(CPU& cpu) {
    original(cpu);
    static uint64_t previous = 0;
    const uint64_t frame = g_frame.load();
    if (frame == previous) return;
    if (previous && frame != previous+1) fatal("Native LAN: unexpected Wonder frame boundary");
    previous = frame;
    play::frame();
    lan::next_frame();
}
}
void lan_init() {
    if (!lan::active()) return;
    const uint64_t base = game_lib().main->base;
    const auto* module = module_at(base);
    for (size_t i=0;i<8;++i)
        if (ld<uint32_t>(base+offset+i*4) != expected[i])
            fatal("Native LAN requires the verified Wonder 1.0.0 framework code");
    if (!module || !(original=module->table[offset/4])) fatal("Native LAN: Wonder frame function is unavailable");
    module->table[offset/4]=framework_frame;
    log("lan: installed complete-framework-frame synchronization at main+e15ec");
}
}
