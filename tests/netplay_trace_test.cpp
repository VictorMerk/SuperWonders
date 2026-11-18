#include "netplay_trace.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <limits>
static void check(bool ok, const char* message) { if (!ok) { std::fprintf(stderr,"%s\n",message); std::exit(1); } }
int main() {
    nx::lan::InputTrace<2> trace;
    nx::lan::TraceEntry entry;
    for (uint64_t tick=1;tick<=3;++tick) {
        entry.tick=tick; entry.frame=tick+10; entry.committed=tick!=3;
        entry.state.valid=true; entry.state.position={1.25f,-2.5f,0};
        entry.local.buttons=std::numeric_limits<uint64_t>::max();
        entry.agreed.players=2; entry.agreed.pads[1].lx=-32767;
        trace.push(entry);
    }
    std::ostringstream out; trace.write(out,1,42);
    const auto text=out.str();
    check(text.find("\n1,11,")==std::string::npos,"oldest entry must be evicted");
    check(text.find("\n2,12,1,42,1,")<text.find("\n3,13,1,42,0,"),"retain chronological committed and failed exchanges");
    check(text.find(",1.25,-2.5,0,18446744073709551615,")!=std::string::npos,"lossless unsigned inputs and portable coordinates");
    check(text.find(",-32767,")!=std::string::npos,"remote stick input retained");
    trace.reset(); std::ostringstream empty; trace.write(empty,0,0);
    check(empty.str().find('\n')==empty.str().size()-1,"new lobby has no stale entries");
    std::puts("LAN trace: bounded ring, ordering, failed exchange, inputs and reset passed");
}
