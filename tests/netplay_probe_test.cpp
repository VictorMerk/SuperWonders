#include "netplay_probe.h"
#include <cstdio>
#include <cstdlib>
static void check(bool ok,const char* message){if(!ok){std::fprintf(stderr,"%s\n",message);std::exit(1);}}
int main(){
    using namespace nx::lan;
    PlayerState left{42,true,{-2.24873567f,2,0}},right{42,true,{1.50128126f,2,0}};
    PositionProbeGuard probe;
    check(probe.observe(left,right,4) && !probe.ready(),"first unassociated probe sample cannot establish divergence");
    for(int n=0;n<4;++n){check(probe.observe(left,left,4),"matching baseline accepted");check(probe.ready()==(n==3),"four agreeing samples establish baseline");}
    check(probe.observe(left,right,4),"single sampled disagreement is not persistent divergence");
    check(probe.observe(left,left,4),"matching sample clears transient disagreement");
    check(probe.observe(left,right,4) && probe.observe(left,right,4) && !probe.observe(left,right,4),"persistent disagreement after a baseline still fails");
    right.scene=43;check(probe.observe(left,right,4) && !probe.ready(),"asynchronous scene discovery invalidates actor association");
    right=left;right.valid=false;check(probe.observe(left,right,4) && !probe.ready(),"missing actor invalidates baseline");
    PositionProbeGuard trusted;right=left;right.position[0]+=1;
    check(!trusted.observe(left,right,0),"trusted explicit state retains immediate strict validation");
    std::puts("position probe baseline, transient sampling, scene invalidation and strict state passed");
}
