// Synchronize at the end of Wonder 1.0.0's full framework frame. A queued NVN
// present is earlier: the framework may still finish work after submitting it.
#include <nx/runtime.h>
#include <nx/gamelib.h>
#include <nx/module.h>
#include "netplay_game.h"
#include "play.h"

namespace nx::wonder {
namespace {
constexpr uint64_t offset = 0xe15ec;
constexpr uint32_t expected[] = {0xd10143ff,0xa9027bfd,0xa90357f6,0xa9044ff4,
                               0x910083fd,0x3949d009,0xaa0003f3,0x7100013f};
Fn original = nullptr;
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
