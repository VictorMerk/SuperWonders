#include "nvn/deferred_bind_policy.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <unordered_map>
using namespace nx::nvn::deferred_bind;
void check(bool b,const char* why){if(!b){fprintf(stderr,"deferred bind: %s\n",why);exit(1);}}
int main(){
    for(uint32_t kind=0;kind<4;++kind)for(bool global:{false,true})for(bool textures:{false,true}) {
        int ordinary=0,deferred=0;Request got;
        Request request{1,9,2,0x1000};
        dispatch(kind,request,global,textures,[&]{++ordinary;},[&](Request r){++deferred;got=r;});
        const bool want=global||(textures&&kind==0);
        check(deferred==int(want)&&ordinary==int(!want),"default/texture-only/global operation selection");
        if(want)check(got.stage==1&&got.first==9&&got.count==2&&got.address==0x1000,"deferred address/stage/range retained");
    }
    for(auto request:{Request{1,9,0,0x1000},Request{1,9,-1,0x1000},Request{1,9,1,0}}) {
        int calls=0;dispatch(0,request,false,true,[&]{++calls;},[&](Request){++calls;});check(!calls,"invalid deferred input records nothing");
    }
    std::array<uint64_t,4> memory{0x1111,2,3,4};
    uint64_t ordinary_value=0;Request recorded;
    dispatch(0,{1,9,1,0x1000},false,false,[&]{ordinary_value=memory[0];},[&](Request r){recorded=r;});
    dispatch(0,{1,9,1,0x1000},false,true,[&]{ordinary_value=memory[0];},[&](Request r){recorded=r;});
    memory[0]=0x2222;
    auto submitted=capture_words(0,uint32_t(recorded.count),[&](size_t i){return memory[i];});
    check(ordinary_value==0x1111&&submitted.size()==1&&submitted[0]==0x2222,"ordinary stays recorded; deferred sees pre-submit mutation");
    memory[0]=0x3333;check(submitted[0]==0x2222,"submit snapshot stable against later mutation");
    check(capture_words(1,2,[&](size_t i){return memory[i];}).size()==2,"images remain one word per binding");
    for(uint32_t kind:{2u,3u})check(capture_words(kind,2,[&](size_t i){return memory[i];}).size()==4,"buffer ranges retain two words");
    int reads=0;check(capture_words(0,65,[&](size_t){++reads;return uint64_t{};}).empty()&&!reads,"oversized snapshot bounded without reads");
    for(bool reverse:{false,true}) {
        std::unordered_map<uint64_t,uint32_t> spans;
        if(reverse){note_span(spans,0x1000,2,2);note_span(spans,0x1000,0,1);}
        else {note_span(spans,0x1000,0,1);note_span(spans,0x1000,2,2);}
        check(spans.size()==1&&spans[0x1000]==4,"same address uses largest word span independent of order and kind");
        auto snapshot=capture_span(spans[0x1000],[&](size_t i){return memory[i];});
        memory[3]=99;check(snapshot.size()==4&&snapshot[3]==4,"larger required tail was captured before later mutation");memory[3]=4;
    }
    puts("deferred texture binds: dispatch, timing, immutable submit, shared word spans and bounds passed");
}
