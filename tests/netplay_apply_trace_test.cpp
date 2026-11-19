#include "netplay_apply_trace.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <thread>
using namespace nx::lan;
static void check(bool ok,const char* why) { if(!ok) { std::cerr << why << '\n';std::exit(1); } }
static std::string csv(const ApplyTrace& trace) { std::ostringstream out; trace.write(out,1,42); return out.str(); }
int main() {
    ApplyTrace trace(4); ApplyTraceEntry e;
    for(uint64_t i=1;i<=8;++i) { e.tick=i; e.event=ApplyEvent::Apply; trace.push(e); }
    auto text=csv(trace);
    check(text.find("capacity=4,total=8,retained=4,omitted=4")!=text.npos,"bounded retention metadata");
    check(text.find("\n1,apply,")<text.find("\n2,apply,") && text.find("\n2,apply,")<text.find("\n7,apply,") && text.find("\n7,apply,")<text.find("\n8,apply,"),"prefix and latest tail chronological");
    check(text.find("\n3,apply,")==text.npos && text.find("count_apply=8")!=text.npos,"omitted middle still counted");
    ApplyTrace partial(7);
    e={}; e.event=ApplyEvent::LoadingDrainDiscard; e.tick=11; e.frame=9; e.ns=700; e.sampled_ns=600;
    e.agreed=true; e.inputs.players=2; e.local_loading_event=88;
    e.inputs.loading_events[1]=99; e.inputs.pads[1].buttons=std::numeric_limits<uint64_t>::max();
    e.inputs.pads[1].lx=-32767; partial.push(e);
    e.event=ApplyEvent::LoadingExchange; partial.push(e);
    e.event=ApplyEvent::LoadingNeutralApply; e.inputs={}; partial.push(e);
    text=csv(partial);
    check(text.find("\n1,loading_drain_discard,700,9,11,600,88,1,2,0,0,0,")!=text.npos,"discard distinct from application with frame/tick/sample age");
    check(text.find(",99,18446744073709551615,-32767,")!=text.npos,"peer markers and pads lossless");
    check(text.find("count_apply=0")!=text.npos && text.find("count_loading_neutral_apply=1")!=text.npos,"loading neutral exposed separately from gameplay apply");
    ApplyTrace hid(2); e.event=ApplyEvent::HidDelivery; e.player=1; e.sampling_number=123; e.caller=456;
    hid.push(e); text=csv(hid);
    check(text.find(",1,123,456,")!=text.npos,"delivered HID player/sample/caller retained");
    ApplyTrace framework(2); e={};e.event=ApplyEvent::FrameworkFrame;e.framework_sequence=77;e.frame_before=e.frame=99;
    framework.push(e);text=csv(framework);
    check(text.find("count_framework_frame=1")!=text.npos&&text.find(",77,99,")!=text.npos,
          "framework invocation without a present is retained separately from input apply");
    ApplyTrace concurrent(8);
    const auto produce=[&] { for(unsigned i=0;i<1000;++i)concurrent.push({}); };
    std::thread a(produce),b(produce); a.join(); b.join();
    text=csv(concurrent);
    check(text.find("total=2000,retained=8,omitted=1992")!=text.npos && text.find("count_initial=2000")!=text.npos,"worker/main concurrent pushes counted without loss");
    ApplyTrace empty(2); text=csv(empty);
    check(text.find("total=0,retained=0,omitted=0")!=text.npos,"empty trace is safe");
    std::cout << "Application trace: bounded prefix/tail, lossless discarded input/markers, counters, concurrency passed; entry bytes=" << sizeof(ApplyTraceEntry) << '\n';
}
