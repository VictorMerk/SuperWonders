#include "loading_music.h"
#include <cstdio>
#include <cstdlib>
static void check(bool ok,const char* message){if(!ok){std::fprintf(stderr,"%s\n",message);std::exit(1);}}
int main(){
    using nx::wonder::LoadingMusicGate;
    constexpr auto rhythm="content://Sound/Resource/Stream/BGM_Course_Savanna_Rhythm.bwav";
    constexpr auto normal="content://Sound/Resource/Stream/BGM_Course_Savanna_Normal.bwav";
    constexpr auto world="content://Sound/Resource/Stream/BGM_World_01.bwav";
    LoadingMusicGate host,client;
    check(host.observe(normal) && client.observe(normal),"initial course music synchronizes");
    unsigned a=0,b=0;
    for(unsigned i=0;i<4;++i)a+=host.observe(rhythm);
    for(unsigned i=0;i<3;++i)b+=client.observe(rhythm);
    check(a==1 && b==1,"four host rhythm opens and three client opens produce one barrier each");
    check(!host.observe("content://Sound/Resource/Stream/Env_Course.bwav") && !host.observe(rhythm),"ambience does not rearm current music");
    check(host.observe(world) && host.observe(normal),"cached map return and course reentry still synchronize");
    check(!host.observe("content://Stage/StageLoadInfo/Course001_Course.game__stage__StageLoadInfo.bgyml") && host.observe(normal),"a new scene load rearms an identical track");
    check(!host.observe("content://Sound/Resource/Stream/BGM_Jingle_PlayerDown.bwav") && host.observe(normal),"death reload rearms the current track");
    check(!host.observe("content://Sound/Resource/Stream/BGM_Jingle_CourseClear.bwav"),"jingles are not loading barriers");
    std::puts("Wonder LAN music: asymmetric streaming reopens, scene transitions and death reloads passed");
}
