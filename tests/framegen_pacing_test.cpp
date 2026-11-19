#include "nvn/framegen_timing.h"
#include <cstdio>
#include <cstdlib>

static void check(bool ok,const char* why) {
    if (!ok) {std::fprintf(stderr,"framegen pacing: %s\n",why);std::exit(1);}
}
int main() {
    using nx::nvn::framegen_present_anchor;
    check(framegen_present_anchor(0,105,16)==105,"first/passthrough frame retains actual time");
    check(framegen_present_anchor(100,105,16)==100,"small completed-present delay does not shift phase");
    check(framegen_present_anchor(100,116,16)==116,"whole missed period resets the phase");
    check(framegen_present_anchor(100,500,16)==500,"long stall cannot create a long catch-up burst");
    check(framegen_present_anchor(100,99,16)==99,"clock reversal resets safely");
    check(framegen_present_anchor(100,105,0)==105,"invalid period cannot retain an old phase");
    for (int cap:{30,60,90,120,144}) {
        const auto period=nx::nvn::framegen_present_period(cap,1);
        const uint64_t start=1'000'000'000, delay=500'000;
        uint64_t anchor=start,previous=0;
        for (unsigned frame=1;frame<=600;++frame) {
            const uint64_t target=anchor+period, presented=target+delay;
            anchor=framegen_present_anchor(target,presented,period);
            check(presented==start+frame*period+delay,"repeated timer/present overhead accumulates into the cap");
            if (previous) check(presented-previous==period,"steady frames must follow the requested cadence");
            previous=presented;
        }
        anchor=framegen_present_anchor(anchor+period,anchor+period*5,period);
        check(anchor==start+605*period,"missed frames restart from actual time");
    }
    std::puts("framegen pacing: steady deadlines at all caps, first frames, reversals and stall resets pass");
}
