// Actual version-guarded production wrappers; no guest assets, game or network.
#include <nx/runtime.h>
#include <nx/gamelib.h>
#include "netplay_apply_trace.h"
#include "netplay_game.h"
#include <array>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>
namespace fixture {
struct Failure {};
std::vector<unsigned char> bytes(0x1de0000);
std::vector<nx::Fn> table(bytes.size()/4);
nx::ModuleInfo info{};nx::GameLib lib{};nx::LoadedModule module{};
bool enabled=true,nesting=false,throw_core=false;
std::array<unsigned,3> calls{};
nx::lan::ObservationRegistry registry;
struct Event {nx::lan::CalcPhase phase;bool enter,threw;uint64_t invocation,parent;};
std::vector<Event> events;
unsigned next_calls=0;
void check(bool ok,const char* message){if(!ok){std::fprintf(stderr,"FAIL %s\n",message);std::abort();}}
}
namespace nx {
std::atomic<uint64_t> g_frame{3};
const GameLib& game_lib(){return fixture::lib;}
const LoadedModule* module_at(uint64_t){return &fixture::module;}
void log(const char*,...){}
[[noreturn]] void fatal(const char*,...){throw fixture::Failure{};}
namespace play {void frame(){std::abort();}}
namespace lan {
bool active(){return true;}
bool trace_phases_enabled() noexcept{return fixture::enabled;}
FrameworkObservation trace_framework_enter(const CPU&) noexcept{return {};}
void trace_framework_leave(const CPU&,FrameworkObservation,bool) noexcept{}
void trace_framework_frame(uint64_t,uint64_t){}
void next_frame(){++fixture::next_calls;}
uint64_t seed(){return 0;}
void set_tick_seed_filter(TickSeedFilter){}
FrameworkObservation trace_phase_enter(const CPU& cpu,CalcPhase phase) noexcept {
 const auto id=fixture::registry.begin(cpu.tpidrro);
 fixture::events.push_back({phase,true,false,id.invocation,id.parent});
 return {id.invocation,id.parent,g_frame.load(),id.thread};
}
void trace_phase_leave(const CPU& cpu,FrameworkObservation id,CalcPhase phase,bool threw) noexcept {
 fixture::events.push_back({phase,false,threw,id.invocation,id.parent});
 fixture::registry.end(cpu.tpidrro,id.invocation);
}
}
}
#include "lan.cpp"
namespace fixture {
void core(nx::CPU& cpu){++calls[1];cpu.x[0]+=10;if(throw_core)throw &info;}
void post(nx::CPU& cpu){++calls[2];cpu.x[0]+=100;}
void pre(nx::CPU& cpu){++calls[0];cpu.x[0]+=1;if(nesting)nx::wonder::observed_phase<nx::lan::CalcPhase::Core>(cpu);}
void frame(nx::CPU&){std::abort();}
void prepare(bool product){
 std::fill(bytes.begin(),bytes.end(),0);std::fill(table.begin(),table.end(),nullptr);
 info={};info.base=reinterpret_cast<uintptr_t>(bytes.data());info.text_size=bytes.size();
 info.build_id=product?"ff773e90972d544eb79406eaa65396d53c43efb9000000000000000000000000":"cd6e42aee7934f4d8393cd43893ae25edc83d338000000000000000000000000";
 lib.main=&info;module={&info,info.base,bytes.size(),table.data()};
 const uint64_t boundary=product?0xcfd24:0xe15ec;
 std::memcpy(bytes.data()+boundary,nx::wonder::expected,sizeof(nx::wonder::expected));table[boundary/4]=frame;
 const nx::Fn fns[]={pre,core,post};
 for(size_t i=0;i<3;++i){auto site=nx::wonder::phase_sites[i];auto words=site.words;
  if(product&&i==2){words[6]=0xd0018995;words[7]=0xb0011d09;}
  const auto offset=product?site.product120:site.original100;
  std::memcpy(bytes.data()+offset,words.data(),32);table[offset/4]=fns[i];
 }
 events.clear();calls={};nesting=false;throw_core=false;
}
}
int main(){using namespace fixture;using namespace nx;
 for(bool product:{false,true}){
  prepare(product);enabled=false;const auto before=table;wonder::lan_init();
  for(size_t i=0;i<3;++i){const auto& site=wonder::phase_sites[i];const auto at=product?site.product120:site.original100;
   check(table[at/4]==before[at/4],"default-off phase entries untouched");}
  for(unsigned failure=0;failure<5;++failure){
   prepare(product);enabled=true;const auto at=product?wonder::phase_sites[2].product120:wonder::phase_sites[2].original100;
   if(failure==0)bytes[at]^=1;
   if(failure==1)table[at/4]=nullptr;
   if(failure==2)info.text=at+4;
   if(failure==3)info.build_id="unknown";
   if(failure==4)module.size=at+16;
   const auto untouched=table;bool failed=false;try{wonder::lan_init();}catch(const Failure&){failed=true;}
   check(failed&&table==untouched,"guard/Fn/build/range failure installs no partial hooks");
  }
  prepare(product);enabled=true;wonder::lan_init();CPU cpu{};cpu.tpidrro=77;
  for(unsigned i=0;i<31;++i)cpu.x[i]=100+i;
  const auto before_cpu=cpu;nesting=true;
  table[(product?wonder::phase_sites[0].product120:wonder::phase_sites[0].original100)/4](cpu);
  check(calls[0]==1&&calls[1]==1&&cpu.x[0]==111,"original Fn exactly once and nested return forwarded");
  for(unsigned i=1;i<31;++i)check(cpu.x[i]==before_cpu.x[i],"nonreturn registers preserved");
  check(events.size()==4&&events[1].parent==events[0].invocation&&events[2].invocation==events[1].invocation&&events[3].invocation==events[0].invocation,"nested local observation pairing");
  check(registry.current(cpu.tpidrro).invocation==0,"normal unwind restores registry");
  events.clear();throw_core=true;bool same_exception=false;
  try{wonder::observed_phase<lan::CalcPhase::Core>(cpu);}catch(ModuleInfo* value){same_exception=value==&info;}
  check(same_exception&&calls[1]==2&&events.size()==2&&events[1].threw,"original exception identity and exactly-once throw exit");
  check(registry.current(cpu.tpidrro).invocation==0,"exception unwind restores registry");
  throw_core=false;wonder::observed_phase<lan::CalcPhase::Post>(cpu);
  check(calls[2]==1&&next_calls==0,"post forwarding does not change network cadence");
 }
 std::puts("Actual Wonder phase wrappers: both versions, default-off, fail-closed atomic installation, forwarding/nesting/exceptions passed");
}
