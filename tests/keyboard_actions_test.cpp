#include "keyboard_actions.h"
#include "overlay/settings.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdlib>
static void check(bool b,const char* text){if(!b){std::fprintf(stderr,"%s\n",text);std::exit(1);}}
int main() {
    using namespace nx;using namespace nx::plat;using namespace nx::input;
    for(auto codes:{KeyboardCodes::Windows,KeyboardCodes::SDL}) {
        const auto keys=recommended_keyboard_bindings(codes);const int standee=default_standee_key(codes);
        KeyboardActionState state;
        auto press=[&](int key,int layout=0,bool actions=true){state={};auto query=[&](int code){return code==key;};keyboard_pad(keys,actions,layout,standee,codes,state,0,query);return keyboard_pad(keys,actions,layout,standee,codes,state,100'000'000,query);};
        const auto jump=press(keys[1]);check(jump.buttons==kB&&jump.lx==0&&jump.ly==0,"Space sends actual Wonder jump B, never use/confirm A");
        check(press(keys[3]).buttons==kY,"Shift sends sprint Y only");
        check(press(keys[5]).buttons==kR,"E sends spin R only");
        auto pound=press(keys[6]);check(pound.buttons==(kZL|kDown)&&pound.ly==-kStickMax,"one ground-pound key supplies both supported down/ZL routes");
        auto placed=press(standee);check(placed.buttons==(kA|kDown)&&placed.ly==-kStickMax,"standee shortcut supplies crouch + use without jumping");
        check(press(keys[0]).buttons==kA&&press(keys[2]).buttons==kX,"use item and greet are distinct actions");
        check(press(keys[8]).buttons==kPlus&&press(keys[9]).buttons==kMinus,"pause and menu stay separate");
        check(press(keys[12]).lx==-kStickMax&&press(keys[14]).lx==kStickMax,"movement is remappable analog and digital");
        auto both=keyboard_pad(keys,true,0,standee,codes,state,0,[&](int code){return code==keys[1]||code==keys[3];});
        check(both.buttons==(kB|kY),"jump and sprint combine without unintended face actions");
        auto conflict=keyboard_pad(keys,true,0,standee,codes,state,0,[&](int code){return code==keys[6]||code==keys[13];});
        check(conflict.ly==-kStickMax&&!(conflict.buttons&kUp),"dedicated ground pound overrides look-up input");
        auto neutral=keyboard_pad(keys,true,0,standee,codes,state,0,[&](int code){return code==keys[12]||code==keys[14];});
        check(neutral.lx==0&&!(neutral.buttons&(kLeft|kRight)),"opposite directions cancel");
        check(press(keys[1],1).buttons==kA&&press(keys[3],1).buttons==kB,"alternate game setting retains jump/sprint keys");
        check(press(keys[0],1).buttons==kX&&press(keys[2],1).buttons==kY,"alternate use/emote rotation");
        check(press(standee,1).buttons==(kX|kDown),"alternate standee uses rotated item button");
        check(press(keys[6],0,false).buttons==kZL&&press(standee,0,false).buttons==0,"legacy mode does not add keyboard-only combinations");
        check(press(0).buttons==0,"unbound slots do not synthesize input");
        state={};
        auto first=keyboard_pad(keys,true,0,standee,codes,state,0,[&](int code){return code==standee;});
        check(first.buttons==kDown,"standee crouches before use");
        auto queued=keyboard_pad(keys,true,0,standee,codes,state,100'000'000,[](int){return false;});
        check(queued.buttons==(kDown|kA),"a short standee tap completes crouch then use");
        keyboard_pad(keys,true,0,standee,codes,state,200'000'000,[](int){return false;});
        check(keyboard_pad(keys,true,0,standee,codes,state,201'000'000,[](int){return false;}).buttons==0,"standee releases after sequence");
        state={};
        auto rebound=keys;rebound[1]=199;
        check(keyboard_pad(rebound,true,0,standee,codes,state,0,[&](int code){return code==keys[1];}).buttons==0,"rebinding removes the previous jump key");
        check(keyboard_pad(rebound,true,0,standee,codes,state,0,[](int code){return code==199;}).buttons==kB,"rebound key retains jump semantics");
        for(size_t game_slot=0;game_slot<4;++game_slot) {
            constexpr uint64_t bits[]={kA,kB,kX,kY};
            check(game_buttons(bits[canonical_keyboard_slot(game_slot,1)],1)==bits[game_slot],"prompt inverse agrees with input rotation");
        }
    }
    const auto path=std::filesystem::temp_directory_path()/("wonder-action-keys-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".ini");
    overlay::Settings settings;settings.keyboard_game_layout=1;settings.keyboard_standee_key=199;settings.keyboard_bindings[1]=198;
    overlay::save_settings(path.string(),settings);overlay::Settings loaded;
    check(overlay::load_settings(path.string(),loaded)&&loaded.keyboard_actions&&loaded.keyboard_game_layout==1&&loaded.keyboard_standee_key==199&&loaded.keyboard_bindings[1]==198,"action mode, layout and custom shortcuts persist");
    {std::ofstream f(path);auto old=version_one_keyboard_bindings(native_keyboard_codes());f<<"keyboard_layout_version=1\n";for(size_t i=0;i<old.size();++i)f<<"keyboard_"<<i<<'='<<old[i]<<'\n';}
    check(overlay::load_settings(path.string(),loaded)&&loaded.keyboard_actions&&loaded.keyboard_bindings==recommended_keyboard_bindings(),"previous incorrect preset is upgraded to actual Wonder actions");
    {std::ofstream f(path);auto old=version_one_keyboard_bindings(native_keyboard_codes());old[1]=199;f<<"keyboard_layout_version=1\n";for(size_t i=0;i<old.size();++i)f<<"keyboard_"<<i<<'='<<old[i]<<'\n';}
    check(overlay::load_settings(path.string(),loaded)&&!loaded.keyboard_actions&&loaded.keyboard_bindings[1]==199,"custom previous layouts stay in raw button mode");
    std::filesystem::remove(path);std::puts("PC action translation, combinations, alternate layout, rebinding, prompts and migration passed");
}
