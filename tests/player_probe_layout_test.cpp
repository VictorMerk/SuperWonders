#include "player_probe_layout.h"
#include "play_scene.h"
#include <cstdlib>
#include <iostream>
void check(bool ok,const char* message){if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
int main() {
    using nx::play::verified_player_layout;
    check(nx::play::map_scene_resource("content:/Bake/Scene/World001.bkres.zs")=="World001","cached map return resource identified");
    check(nx::play::map_scene_resource("content://Bake/Scene/World002.bkres.zs")=="World002","double-separator mount supported");
    for(const auto path:{"content:/Bake/Scene/World001.bkres.zs.bak","content:/Bake/Scene/World001Extra.bkres.zs","content:/Bake/Scene/WorldABC.bkres.zs","content:/Bake/Scene/Course001.bkres.zs"})
        check(nx::play::map_scene_resource(path).empty(),"only exact numbered map scene resources rearm map");
    const auto original=verified_player_layout("cd6e42aee7934f4d8393cd43893ae25edc83d338000000000000000000000000",0x4000000000);
    check(original.vt==0x400096e400&&original.pos==0xc00,"verified original layout retained");
    const auto updated=verified_player_layout("ff773e90972d544eb79406eaa65396d53c43efb9000000000000000000000000",0x4000000000);
    check(updated.vt==0&&updated.pos==0,"updated build cannot scan using original actor addresses");
    check(verified_player_layout("",0x4000000000).vt==0&&verified_player_layout("unknown",0x4000000000).vt==0,"missing/unknown identity fails closed");
    check(verified_player_layout("cd6e42aee7934f4d8393cd43893ae25edc83d338000000000000000000000001",0x4000000000).vt==0,"nearby source identities do not inherit a layout");
    std::cout<<"source-specific player probe layout tests passed\n";
}
