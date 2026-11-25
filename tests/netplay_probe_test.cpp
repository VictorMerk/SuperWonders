#include "netplay_probe.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
static void check(bool ok,const char* message){if(!ok){std::fprintf(stderr,"%s\n",message);std::exit(1);}}
int main(){
    using namespace nx::lan;
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const float inf=std::numeric_limits<float>::infinity();
    check(usable_probe_position(std::array<float,3>{0,0,0}),"finite origin is a usable live observation");
    check(usable_probe_position(std::array<float,3>{-1e6f,1e6f,2}),"wire-compatible coordinate boundary accepted");
    check(!usable_probe_position(std::array<float,3>{1,nan,0}) &&
          !usable_probe_position(std::array<float,3>{1,0,nan}) &&
          !usable_probe_position(std::array<float,3>{inf,0,0}) &&
          !usable_probe_position(std::array<float,3>{0,0,1000001}),
          "every probe coordinate must be finite and inside the wire bounds");
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
