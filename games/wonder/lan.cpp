// Synchronize at the end of Wonder 1.0.0's full framework frame. A queued NVN
// present is earlier: the framework may still finish work after submitting it.
#include <nx/runtime.h>
#include <nx/gamelib.h>
#include <nx/module.h>
#include "netplay_game.h"
#include "play.h"
#include "lan_seed.h"
#include "version.h"
#include <atomic>
#include <array>

namespace nx::wonder {
namespace {
constexpr uint64_t offset = 0xe15ec;
constexpr uint32_t expected[] = {0xd10143ff,0xa9027bfd,0xa90357f6,0xa9044ff4,
                               0x910083fd,0x3949d009,0xaa0003f3,0x7100013f};
Fn original = nullptr;
// These source-named task callbacks include lifecycle/wait paths. They are
// observations only; an invocation is never treated as a simulation step.
struct PhaseSite { uint64_t original100,product120; std::array<uint32_t,8> words; };
constexpr std::array<PhaseSite,3> phase_sites{{
    {0x318200,0x312dc0,{0xd10203ff,0xa9027bfd,0xa9036ffc,0xa90467fa,0xa9055ff8,0xa90657f6,0xa9074ff4,0x910083fd}},
    {0x1caead4,0x1dd7df4,{0xd10103ff,0xa9017bfd,0xa90257f6,0xa9034ff4,0x910043fd,0xb9431808,0xaa0003f3,0x71000d1f}},
    {0x68aec4,0x68d014,{0xd10183ff,0xa9027bfd,0xa9035ff8,0xa90457f6,0xa9054ff4,0x910083fd,0x90017d55,0xb0011369}}
}};
Fn phase_originals[3]{};
template<lan::CalcPhase Phase> void observed_phase(CPU& cpu) {
    const auto observation=lan::trace_phase_enter(cpu,Phase);
    try {phase_originals[unsigned(Phase)](cpu);}
    catch(...) {lan::trace_phase_leave(cpu,observation,Phase,true);throw;}
    lan::trace_phase_leave(cpu,observation,Phase);
}
constexpr Fn phase_wrappers[]={observed_phase<lan::CalcPhase::Pre>,observed_phase<lan::CalcPhase::Core>,observed_phase<lan::CalcPhase::Post>};
uint64_t seed_tick(uint64_t caller,uint64_t real_tick) {
    const auto* main=game_lib().main;
    if (!main || caller<main->base) return real_tick;
    const auto version=source_version();
    if (version!=Version::Original100 && version!=Version::Product120) return real_tick;
    const bool product120=version==Version::Product120;
    const auto* site=lan_seed::find(caller-main->base,product120);
    if (!site) return real_tick;
    std::array<uint32_t,8> words;
    for (size_t i=0;i<words.size();++i) words[i]=ld<uint32_t>(main->base+site->call+i*4);
    if (!lan_seed::verified(*site,words)) fatal("Native LAN: RNG seed code does not match the selected source build");
    static std::atomic<uint32_t> logged{0};
    const uint32_t bit=1u<<unsigned(site-lan_seed::table(product120).data());
    if (!(logged.fetch_or(bit)&bit))
        log("lan: shared-session RNG initialization at main+%llx (ordinary clocks unchanged)",(unsigned long long)site->call);
    return lan_seed::value(lan::seed(),*site);
}
// Register before guest module constructors run; after_init is too late for
// static random generators. Atomic filter storage has constant initialization.
const bool seed_filter_registered=[] { lan::set_tick_seed_filter(seed_tick); return true; }();
void framework_frame(CPU& cpu) {
    const auto frame_before=g_frame.load();
    const auto observation=lan::trace_framework_enter(cpu);
    try { original(cpu); }
    catch(...) {lan::trace_framework_leave(cpu,observation,true);throw;}
    lan::trace_framework_leave(cpu,observation);
    static uint64_t previous = 0;
    const uint64_t frame = g_frame.load();
    lan::trace_framework_frame(frame_before,frame);
    if (frame == previous) return;
    if (previous && frame != previous+1) fatal("Native LAN: unexpected Wonder frame boundary");
    previous = frame;
    play::frame();
    lan::next_frame();
}
}
void lan_init() {
    if (!lan::active()) return;
    const auto version=source_version();
    if (version!=Version::Original100 && version!=Version::Product120)
        fatal("Native LAN has no verified framework layout for this source build");
    const uint64_t boundary=version==Version::Product120?0xcfd24:offset;
    const auto* info=game_lib().main;
    const uint64_t base=info->base;
    const auto* module=module_at(base);
    const auto checked_site=[&](uint64_t site,const uint32_t* words,size_t count) {
        if(!module||!module->table||module->info!=info||site<info->text||
           site-info->text>info->text_size||count*4>info->text_size-(site-info->text)||
           site>module->size||count*4>module->size-site)
            fatal("Native LAN: observation source range is unavailable");
        for(size_t i=0;i<count;++i)
            if(ld<uint32_t>(base+site+i*4)!=words[i])
                fatal("Native LAN requires the verified framework/phase code for this source build");
        const Fn fn=module->table[site/4];
        if(!fn)fatal("Native LAN: observation source function is unavailable");
        return fn;
    };
    const Fn frame_fn=checked_site(boundary,expected,8);
    const bool phases=lan::trace_phases_enabled();
    std::array<uint64_t,3> sites{};std::array<Fn,3> functions{};
    if(phases)for(size_t i=0;i<phase_sites.size();++i) {
        const auto& source=phase_sites[i];
        sites[i]=version==Version::Product120?source.product120:source.original100;
        auto words=source.words;
        if(i==2&&version==Version::Product120){words[6]=0xd0018995;words[7]=0xb0011d09;}
        functions[i]=checked_site(sites[i],words.data(),words.size());
        if(functions[i]==phase_wrappers[i])fatal("Native LAN: phase observation already installed");
    }
    if(frame_fn==framework_frame)fatal("Native LAN: framework observation already installed");
    // All validation precedes every table write. Installation is performed at
    // the existing quiescent LAN initialization boundary, before guest work.
    original=frame_fn;
    if(phases)for(size_t i=0;i<sites.size();++i)phase_originals[i]=functions[i];
    module->table[boundary/4]=framework_frame;
    if(phases)for(size_t i=0;i<sites.size();++i)module->table[sites[i]/4]=phase_wrappers[i];
    if(phases)log("lan: source-named pre/core/post observations enabled (local identities only; not simulation/readiness proof)");
    log("lan: installed complete-framework-frame synchronization at main+%llx",(unsigned long long)boundary);
}
}
