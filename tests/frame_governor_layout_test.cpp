#include "frame_governor_layout.h"
#include <cstdlib>
#include <iostream>
void check(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
int main(){
    check(nx::frame_governor_return_site("cd6e42aee7934f4d8393cd43893ae25edc83d338000000000000000000000000")==0xe16ac,"original verified governor retained");
    check(nx::frame_governor_return_site("ff773e90972d544eb79406eaa65396d53c43efb9000000000000000000000000")==0xcfde4,"Product120 uses its verified governor call site");
    check(!nx::frame_governor_return_site("")&&!nx::frame_governor_return_site("ff773e90972d544eb79406eaa65396d53c43efb9000000000000000000000001"),"unknown and near-match builds cannot patch framework memory");
}
