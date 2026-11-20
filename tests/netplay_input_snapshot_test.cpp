#include "netplay_input_snapshot.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

static void check(bool value,const char* message) {
    if(!value){std::fprintf(stderr,"%s\n",message);std::exit(1);}
}
static nx::lan::FrameInputs frame(uint64_t tick) {
    nx::lan::FrameInputs value;
    value.tick=tick;value.players=2;
    value.pads[0]={tick+1000,int32_t(tick),-int32_t(tick),int32_t(tick+1),-int32_t(tick+1)};
    value.pads[1]={tick+2000,int32_t(tick+2),-int32_t(tick+2),int32_t(tick+3),-int32_t(tick+3)};
    return value;
}
int main() {
    using namespace nx::lan;
    InputPublication published;
    auto read=[&](uint32_t player){return published.read(player,[]{return InputReadMetadata{true,0};});};
    auto put=[&](const FrameInputs& value,bool reset=false) {
        std::lock_guard guard(published.mutex());
        return reset?published.reset_locked(value):published.publish_locked(value);
    };
    check(read(0).sampling_number==0&&!read(0).connected,"unpublished state has no fabricated sample");
    check(put(frame(0),true),"publish actual lobby tick zero");
    const auto first=read(0);
    check(first.connected&&first.players==2&&first.committed_tick==0&&first.sampling_number==1,
          "lobby tick zero is distinct from no publication");
    for(unsigned repeat=0;repeat<100;++repeat) {
        const auto same=read(0);
        check(same.sampling_number==first.sampling_number&&same.pad.buttons==first.pad.buttons,
              "repeated reads without publication keep both sample identity and payload");
    }
    check(put(frame(1)),"next agreed input can publish without a present");
    check(read(0).sampling_number==2&&read(0).pad.buttons==1001,
          "changed published commit gets a new HID identity independently of presentation");
    for(uint64_t loading_tick=2;loading_tick<100;++loading_tick) {
        // Runtime transport loading pumps deliberately do not call publish.
        const auto pump=frame(loading_tick);(void)pump;
        check(read(0).sampling_number==2&&read(0).committed_tick==1,
              "transport-only loading commits never invent exposed controller samples");
    }
    FrameInputs neutral;neutral.tick=100;neutral.players=2;
    check(put(neutral),"publish only the existing final loading-neutral exposure");
    const auto loaded=read(1);
    check(loaded.committed_tick==100&&loaded.sampling_number==101&&!loaded.pad.buttons&&!loaded.pad.lx,
          "final neutral publication has an exact new identity, not one sample per loading pump");
    check(!read(2).connected&&!read(4).connected&&!read(UINT32_MAX).connected&&!read(4).pad.buttons,
          "invalid and disconnected slots never expose another player's pad");
    const auto inactive=published.read(0,[]{return InputReadMetadata{false,77};});
    check(!inactive.connected&&!inactive.pad.buttons&&inactive.local_loading_event==77,
          "inactive metadata and neutral delivered pad are observed together");
    check(put(neutral),"identical repeat of an exposed tick is harmless");
    auto changed=neutral;changed.pads[0].buttons=1;
    check(!put(changed)&&!read(0).pad.buttons,"same sampling identity cannot be reused for changed buttons");
    changed=neutral;changed.pads[1].ry=123;
    check(!put(changed)&&!read(1).pad.ry,"same sampling identity cannot be reused for changed analog input");
    changed=neutral;changed.players=1;
    check(!put(changed)&&read(1).connected,"same identity cannot silently change connected player count");
    check(!put(frame(99))&&read(0).committed_tick==100,"backward publication is rejected without changing exposed input");
    auto largest=frame(uint64_t(INT64_MAX)-1);
    check(put(largest)&&read(0).sampling_number==uint64_t(INT64_MAX),"largest positive signed HID sample is representable");
    auto exhausted=largest;exhausted.tick=uint64_t(INT64_MAX);
    check(!put(exhausted)&&read(0).sampling_number==uint64_t(INT64_MAX),"signed sample exhaustion never wraps");
    exhausted.tick=UINT64_MAX;
    check(!put(exhausted),"uint64 tick overflow cannot become sample zero");
    exhausted=largest;exhausted.players=5;
    check(!put(exhausted),"unsupported player count is rejected");

    // Exercise the actual publication/read component with the same mutex and
    // metadata callback used by netplay_game. A reader must never combine an old
    // pad with a new tick/sample/loading marker while publications race it.
    InputPublication concurrent;
    uint64_t marker=0; // every access is inside concurrent.mutex()
    {std::lock_guard guard(concurrent.mutex());check(concurrent.reset_locked(frame(0)),"concurrent initial frame");}
    std::atomic<bool> start=false,done=false,coherent=true;
    std::atomic<uint64_t> observations=0;
    auto reader=[&](uint32_t player) {
        while(!start.load())std::this_thread::yield();
        do {
            const auto observed=concurrent.read(player,[&]{return InputReadMetadata{true,marker};});
            const auto tick=observed.committed_tick;
            const auto expected=frame(tick).pads[player];
            if(!observed.connected||observed.players!=2||observed.player!=player||observed.sampling_number!=tick+1||
               observed.local_loading_event!=tick*7||observed.pad.buttons!=expected.buttons||
               observed.pad.lx!=expected.lx||observed.pad.ly!=expected.ly||
               observed.pad.rx!=expected.rx||observed.pad.ry!=expected.ry)coherent=false;
            ++observations;
        } while(!done.load());
    };
    std::thread left(reader,0),right(reader,1);
    start=true;
    for(uint64_t tick=1;tick<=20000;++tick) {
        std::lock_guard guard(concurrent.mutex());
        if(!concurrent.publish_locked(frame(tick)))coherent=false;
        marker=tick*7;
    }
    done=true;left.join();right.join();
    check(coherent&&observations>=2,"concurrent publication/read preserves exact pad, identity and control metadata");
    std::puts("LAN HID publication: tick identities, repeated reads, loading exposure, bounds and concurrent snapshots passed");
}
