#include "nvn/bounded_frame_pair.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>
using Pair=nx::nvn::diagnostic::BoundedFramePair;
void check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"frame pair: %s\n",why);std::exit(1);}}
int main(){
    Pair good;check(good.next(1,1)==Pair::Step::Idle,"default is disabled");
    check(good.begin(50,1000),"different counter origins accepted");
    for(uint64_t i=1;i<=6;++i)check(good.next(50+i,1000+i)==Pair::Step::Capture,"exact next six frames captured");
    check(!good.active()&&!good.failed()&&good.next(57,1007)==Pair::Step::Idle,"window ends after six");
    check(!good.begin(100,200),"one shot cannot silently rearm");
    for(auto bad:{0u,7u,100u}){Pair p;check(!p.begin(1,1,bad)&&!p.active(),"invalid count cannot arm");}
    for(bool guest:{false,true}){Pair p;auto max=std::numeric_limits<uint64_t>::max();check(!p.begin(guest?max-5:1,guest?1:max-5),"counter overflow rejected");}
    for(int mode=0;mode<4;++mode){
        Pair p;check(p.begin(10,100),"mismatch case arms");
        const auto frame=mode==0?10u:mode==1?12u:11u;
        const auto present=mode==2?100u:mode==3?102u:101u;
        check(p.next(frame,present)==Pair::Step::Mismatch&&p.failed()&&!p.active(),"repeat or skipped counter fails closed");
        check(p.next(11,101)==Pair::Step::Idle,"no later capture after mismatch");
    }
    Pair late;check(late.begin(20,30)&&late.next(21,31)==Pair::Step::Capture,"first paired frame");
    check(late.next(22,33)==Pair::Step::Mismatch,"mid-window presentation gap rejects remaining evidence");
    Pair canceled;check(canceled.begin(1,10),"cancel case arms");canceled.cancel();
    check(!canceled.active()&&canceled.failed()&&canceled.next(2,11)==Pair::Step::Idle,"unsupported mode cancels captures");
    std::puts("bounded frame pair: disabled, exact six, offsets, count/overflow and sequence failures passed");
}
