#include "netplay_trace.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <limits>
#include <algorithm>
static void check(bool ok, const char* message) { if (!ok) { std::fprintf(stderr,"%s\n",message); std::exit(1); } }
int main() {
    nx::lan::InputTrace<2> trace;
    nx::lan::TraceEntry entry;
    for (uint64_t tick=1;tick<=3;++tick) {
        entry.tick=tick; entry.frame=tick+10; entry.committed=tick!=3;
        entry.state.valid=true; entry.state.position={1.25f,-2.5f,0};
        entry.local.buttons=std::numeric_limits<uint64_t>::max();
        entry.agreed.players=2; entry.agreed.pads[1].lx=-32767;
        entry.loading_event=100+tick; entry.agreed.loading_events[1]=200+tick;
        entry.sampled_ns=1000;entry.exchange_started_ns=1200;entry.exchange_finished_ns=1700;
        trace.push(entry);
    }
    std::ostringstream out; trace.write(out,1,42);
    const auto text=out.str();
    check(text.find("\n1,11,")==std::string::npos,"oldest entry must be evicted");
    check(text.find("\n2,12,1,42,1,")<text.find("\n3,13,1,42,0,"),"retain chronological committed and failed exchanges");
    check(text.find(",1.25,-2.5,0,18446744073709551615,")!=std::string::npos,"lossless unsigned inputs and portable coordinates");
    check(text.find(",-32767,")!=std::string::npos,"remote stick input retained");
    check(text.find(",local_loading_event,exchange_started_ns,exchange_finished_ns,p0_loading_event,p1_loading_event,p2_loading_event,p3_loading_event\n")!=std::string::npos,"append explicit loading and local monotonic timing columns");
    check(text.find(",102,1200,1700,0,202,0,0\n")!=std::string::npos,"retain the sent marker and each peer loading marker");
    std::istringstream csv(text);std::string line;
    std::getline(csv,line);const auto columns=std::count(line.begin(),line.end(),',');
    while(std::getline(csv,line))check(std::count(line.begin(),line.end(),',')==columns,"every retained CSV row matches the extended header");
    trace.reset(); std::ostringstream empty; trace.write(empty,0,0);
    check(empty.str().find('\n')==empty.str().size()-1,"new lobby has no stale entries");
    std::puts("LAN trace: bounded ring, ordering, failed exchange, inputs and reset passed");
}
