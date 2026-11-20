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
    check(text.find("hid_identity=")==text.npos,"legacy/unspecified trace export does not invent atomic HID identity");
    std::ostringstream atomic_hid;hid.write(atomic_hid,1,42,true);
    check(atomic_hid.str().find("hid_identity=exposed_commit_plus_one")!=text.npos,
          "runtime can declare the atomic committed-input sampling contract without changing schema or columns");
    ApplyTrace framework(2); e={};e.event=ApplyEvent::FrameworkFrame;e.framework_sequence=77;e.frame_before=e.frame=99;
    framework.push(e);text=csv(framework);
    check(text.find("count_framework_frame=1")!=text.npos&&text.find(",77,99,")!=text.npos,
          "framework invocation without a present is retained separately from input apply");
    ObservationRegistry registry;
    const auto outer=registry.begin(1234),inner=registry.begin(1234);
    check(outer.thread==inner.thread&&outer.invocation&&inner.parent==outer.invocation,"local nested invocation identity");
    check(registry.current(1234).invocation==inner.invocation,"file observation sees current invocation");
    registry.end(1234,inner.invocation);
    check(registry.current(1234).invocation==outer.invocation,"nested exit restores outer invocation");
    registry.end(1234,outer.invocation);
    check(registry.current(1234).invocation==0,"exit clears current invocation");
    check(registry.current(0).thread==0,"unknown thread token fails unknown");
    for(uint64_t token=1;token<=63;++token)check(registry.current(token).thread!=0,"bounded thread slots available");
    check(registry.current(9999).thread==0,"thread registry exhaustion is explicit unknown");
    ObservationRegistry depth;
    std::array<ObservationIdentity,9> nested{};
    for(auto& identity:nested)identity=depth.begin(55);
    check(depth.current(55).invocation==0,"nesting overflow does not falsely attribute loading file");
    depth.end(55,nested.back().invocation);
    check(depth.current(55).invocation==nested[7].invocation,"overflow unwinding restores tracked nesting");
    ApplyTrace concurrent(8);
    const auto produce=[&] { for(unsigned i=0;i<1000;++i)concurrent.push({}); };
    std::thread a(produce),b(produce); a.join(); b.join();
    text=csv(concurrent);
    check(text.find("total=2000,retained=8,omitted=1992")!=text.npos && text.find("count_initial=2000")!=text.npos,"worker/main concurrent pushes counted without loss");
    ApplyTrace empty(2); text=csv(empty);
    check(text.find("total=0,retained=0,omitted=0")!=text.npos,"empty trace is safe");
    std::cout << "Application trace: bounded prefix/tail, lossless discarded input/markers, counters, concurrency passed; entry bytes=" << sizeof(ApplyTraceEntry) << '\n';
}
