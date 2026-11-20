#include "overlay/pad_navigation_gate.h"
#include <cstdio>
#include <cstdlib>
#include <future>
#include <limits>
#include <thread>

using Gate=nx::overlay::PadNavigationGate;
namespace {
constexpr uint64_t A=1,B=2,L=4,R=8,Plus=16,Minus=32,Combo=Plus|Minus;
void check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"pad gate: %s\n",why);std::exit(1);}}
auto put(Gate& gate,Gate::Sample sample){return gate.publish(gate.begin_sample(),sample,Combo);}
void fresh_neutral(Gate& gate){check(put(gate,{}).accepted,"fresh neutral accepted");}
void opened(Gate& gate){check(gate.set_open(true).changed,"menu opens");fresh_neutral(gate);check(gate.consume().navigation_allowed,"fresh open neutral releases navigation");}
}
int main(){
    {
        Gate gate;
        fresh_neutral(gate);put(gate,{A});
        auto frame=gate.consume();
        check(!frame.state.open&&frame.navigation_allowed&&frame.nav.buttons==A&&frame.pressed==A,
              "launcher can navigate when settings are closed");
        check(!gate.consume().pressed,"edges consumed once");
        opened(gate);
        put(gate,{B});put(gate,{});put(gate,{L|R});
        frame=gate.consume();check(frame.pressed==(B|L|R),"short accepted edges retained in one bounded mailbox");
    }
    {
        Gate gate;opened(gate);
        std::promise<void> begun,release;
        auto ready=begun.get_future();auto go=release.get_future().share();
        Gate::PublishResult result;
        std::thread producer([&]{const auto ticket=gate.begin_sample();begun.set_value();go.wait();result=gate.publish(ticket,{B|L|R|Combo},Combo);});
        check(ready.wait_for(std::chrono::seconds(5))==std::future_status::ready,"paused producer entered");
        const auto old=gate.generation();gate.set_open(false);gate.set_open(true);
        check(gate.generation()==old+2,"close/reopen advances twice without a rendered frame");
        release.set_value();producer.join();
        auto frame=gate.consume();
        check(!result.accepted&&gate.is_open()&&!frame.pressed&&!frame.nav.buttons,
              "old-generation producer cannot publish B/L/R or toggle a reopened menu");
        check(frame.quarantined,"reopened menu waits for a new neutral poll");
        fresh_neutral(gate);put(gate,{B});check(gate.consume().pressed==B,"new-generation edge is accepted after release");
    }
    {
        Gate gate;opened(gate);
        gate.set_capture(true);fresh_neutral(gate);
        const auto old_neutral=gate.begin_sample();
        gate.set_capture(false);
        for(int i=0;i<8;++i)check(!gate.consume().navigation_allowed,"cached neutral cannot end post-capture quarantine");
        check(gate.publish(old_neutral,{}).accepted,"in-generation pending neutral can publish raw feedback");
        check(!gate.consume().navigation_allowed,"poll begun before capture ended is not fresh neutral");
        for(auto held:{B,L,R,A}){
            put(gate,{held});auto frame=gate.consume();
            check(frame.raw.buttons==held&&!frame.nav.buttons&&!frame.pressed&&frame.quarantined,
                  "binding hold remains raw feedback only after capture");
        }
        fresh_neutral(gate);auto released=gate.consume();check(released.navigation_allowed&&!released.pressed,"fresh released pad arms navigation without an edge");
        put(gate,{B});check(gate.consume().pressed==B,"only a genuinely new B becomes navigation");
    }
    {
        Gate gate;opened(gate);
        auto early=gate.begin_sample(),later=gate.begin_sample();
        check(gate.publish(later,{R}).accepted,"newer producer publishes");
        check(!gate.publish(early,{}).accepted&&!gate.publish(later,{L}).accepted,"late and duplicate tickets rejected");
        auto frame=gate.consume();check(frame.raw.buttons==R&&frame.pressed==R,"late publication cannot replace newer sample/edge");
        gate.set_capture(true);put(gate,{A});frame=gate.consume();
        check(frame.raw.buttons==A&&!frame.nav.buttons,"capture gates nav while preserving raw feedback");
    }
    {
        Gate gate;
        auto toggled=put(gate,{Combo});
        check(toggled.accepted&&toggled.transition.changed&&gate.is_open(),"fresh chord toggles without external callback");
        auto held=put(gate,{Combo});check(held.accepted&&!held.transition.changed&&gate.is_open(),"held chord never toggles twice");
        fresh_neutral(gate);gate.set_capture(true);
        toggled=put(gate,{Combo});check(toggled.transition.changed&&!gate.is_open(),"reserved chord can close while capturing");
        check(gate.state().generation==toggled.transition.state.generation,"returned transition can be inspected after unlock");
        gate.set_open(true);put(gate,{Combo});check(gate.is_open(),"chord held across keyboard reopen is not a new toggle");
    }
    {
        Gate gate;opened(gate);gate.set_capture(true);gate.set_capture(false);
        put(gate,{0,std::numeric_limits<int32_t>::min(),0});check(!gate.consume().navigation_allowed,"minimum signed axis is non-neutral without abs overflow");
        put(gate,{0,Gate::kNeutralStickLimit+1,0});check(!gate.consume().navigation_allowed,"axis outside deadzone retains quarantine");
        put(gate,{0,-Gate::kNeutralStickLimit,Gate::kNeutralStickLimit});check(gate.consume().navigation_allowed,"deadzone endpoints are neutral");
    }
    {
        Gate gate;opened(gate);gate.set_capture(true);gate.set_capture(false);
        put(gate,{0,0,0,B});
        check(!gate.consume().navigation_allowed,"async remap logical zero is not physical release");
        put(gate,{B,0,0,B});
        check(!gate.consume().navigation_allowed,"new mapping of held physical button stays quarantined");
        fresh_neutral(gate);check(gate.consume().navigation_allowed,"actual physical release arms navigation");
        put(gate,{B,0,0,B});check(gate.consume().pressed==B,"press after physical release navigates");
    }
    {
        Gate gate;opened(gate);put(gate,{A,0,0,A});gate.consume();
        const auto before_change=gate.begin_sample();
        gate.quarantine_navigation(); // preset/reset/direct assignment, no capture
        check(gate.publish(before_change,{}).accepted,"pre-change snapshot can publish feedback");
        check(!gate.consume().navigation_allowed,"pre-change neutral cannot release new bindings");
        put(gate,{B,0,0,A});
        auto frame=gate.consume();
        check(frame.raw.buttons==B&&!frame.pressed&&!frame.nav.buttons,
              "held A remapped to B cannot cancel after a preset change");
        put(gate,{0,0,0,A});check(!gate.consume().navigation_allowed,"async logical zero is not physical preset release");
        fresh_neutral(gate);check(gate.consume().navigation_allowed,"post-change physical release allows navigation");
        put(gate,{B,0,0,A});check(gate.consume().pressed==B,"fresh press after preset release is actionable");
    }
    std::puts("pad navigation gate: generations, paused producers, fresh neutral, capture, binding changes, launcher and combo ordering passed");
}
