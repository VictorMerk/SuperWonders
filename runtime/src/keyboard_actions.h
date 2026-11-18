#pragma once
#include "keyboard_defaults.h"
#include "platform.h"
namespace nx::input {
// Canonical semantic slots match the standard Wonder game setting (B jump / Y dash).
// An alternate in-game setting rotates the four face buttons; keys remain action-based.
constexpr uint64_t game_buttons(uint64_t canonical,int layout) {
    using namespace plat;
    if(layout!=1)return canonical;
    uint64_t result=canonical&~(kA|kB|kX|kY);
    if(canonical&kA)result|=kX;
    if(canonical&kB)result|=kA;
    if(canonical&kX)result|=kY;
    if(canonical&kY)result|=kB;
    return result;
}
constexpr size_t canonical_keyboard_slot(size_t game_slot,int layout) {
    if(layout!=1 || game_slot>=4)return game_slot;
    constexpr size_t inverse[]={1,3,0,2};return inverse[game_slot];
}
constexpr const char* keyboard_action_name(size_t slot) {
    constexpr const char* names[]={"Use stored item / Confirm","Jump / Swim","Greet / Emote","Sprint / Grab / Throw",
        nullptr,"Spin / Badge ability","Ground pound",nullptr,"Pause","Badges / Map menu",nullptr,nullptr,
        "Move left","Look up / Enter","Move right","Crouch / Move down"};
    return slot<16?names[slot]:nullptr;
}
// Crouch before use; a short tap completes the combination, independent of polling rate.
// Callers reset state when focus or menu ownership changes.
struct KeyboardActionState { bool standee_down=false,standee_pending=false; uint64_t standee_started=0; };
template<class Query> plat::PadState keyboard_pad(const KeyboardBindings& keys,bool actions,int layout,
                                                 int standee_key,KeyboardCodes codes,KeyboardActionState& state,uint64_t now_ns,Query query) {
    using namespace plat;
    constexpr uint64_t bits[]={kA,kB,kX,kY,kL,kR,kZL,kZR,kPlus,kMinus,kStickL,kStickR,kLeft,kUp,kRight,kDown};
    uint64_t canonical=0;bool force_down=false;
    for(size_t slot=0;slot<keys.size();++slot) if(keyboard_key_held(keys[slot],codes,query)) {
        canonical|=bits[slot];
        if(actions && slot==6){canonical|=kDown;force_down=true;}
    }
    const bool standee=actions && keyboard_key_held(standee_key,codes,query);
    if(!actions)state={};
    if(standee && !state.standee_down){state.standee_pending=true;state.standee_started=now_ns;}
    state.standee_down=standee;
    const uint64_t elapsed=now_ns-state.standee_started;
    if(state.standee_pending || standee) {
        canonical|=kDown;force_down=true;
        if(elapsed>=100'000'000)canonical|=kA;
        if(elapsed>=200'000'000)state.standee_pending=false;
    }
    if(force_down)canonical&=~kUp;
    if((canonical&(kLeft|kRight))==(kLeft|kRight))canonical&=~(kLeft|kRight);
    if((canonical&(kUp|kDown))==(kUp|kDown))canonical&=~(kUp|kDown);
    PadState out{};out.buttons=actions?game_buttons(canonical,layout):canonical;
    out.lx=((canonical&kRight?1:0)-(canonical&kLeft?1:0))*kStickMax;
    out.ly=((canonical&kUp?1:0)-(canonical&kDown?1:0))*kStickMax;
    return out;
}
}
