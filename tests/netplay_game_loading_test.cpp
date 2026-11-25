// Characterize the actual runtime publication/loading paths; only host and
// Session I/O are fake. This proves different publication behavior while one
// peer announces loading. It does not execute a game/framework or prove that
// simulation advanced, that this caused a historical desync, or a safe pause fix.
#ifndef NX_VERSION_STRING
#define NX_VERSION_STRING "test"
#endif
#include "netplay_session.h"
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <nx/runtime.h>
#include <sstream>
#include <tuple>
#ifndef _WIN32
#include <unistd.h>
#endif
namespace fixture {
std::deque<nx::lan::FrameInputs> replies;
std::vector<uint64_t> sent_markers;
std::vector<uint64_t> before_ticks;
nx::ModuleInfo main_info{};
nx::LoadedModule main_module{&main_info,0x100000,0x1000,nullptr};
unsigned module_queries=0;
void check(bool ok, const char *m) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", m);
    std::abort();
  }
}
} // namespace fixture
namespace nx {
std::atomic<uint64_t> g_frame{100};
void log(const char *, ...) {}
const LoadedModule* module_at(uint64_t) {++fixture::module_queries;return &fixture::main_module;}
[[noreturn]] void fatal(const char *, ...) { std::abort(); }
namespace plat {
uint64_t monotonic_ns() {
  static std::atomic<uint64_t> now{1};
  return now.fetch_add(1000);
}
void read_pad(PadState &p) { p.buttons = kA; }
std::string data_root() { std::abort(); }
bool headless() { return true; }
[[noreturn]] void exit_process(int) { std::abort(); }
void sleep_ns(int64_t) { std::abort(); }
} // namespace plat
namespace play {
void pad(plat::PadState &) {}
lan::PlayerState netplay_player_state() { return {}; }
uint64_t netplay_state_hash() { return 0; }
} // namespace play
namespace ft {
void input_sample_at(uint64_t, uint64_t) {}
} // namespace ft
namespace lan {
struct Session::Impl {};
Session::Session() = default;
Session::~Session() = default;
void Session::stop() noexcept {}
bool Session::position_probe_ready() const { return false; }
SessionDiagnostics Session::diagnostics() const noexcept { return {}; }
} // namespace lan
} // namespace nx
// The included production file has an existing unused atexit-registration flag.
// Suppress only that warning in this inclusion, not in the fixture code.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include "netplay_game.cpp"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
namespace nx::game {
const Info kInfo = {"test", "test", 0, "test", "test", 0};
}
namespace nx::profiles {
std::filesystem::path user_root(const std::filesystem::path &) { std::abort(); }
} // namespace nx::profiles
namespace nx::lan {
FrameInputs Session::exchange(const plat::PadState &, uint64_t,
                              const PlayerState &, uint64_t, uint64_t marker) {
  fixture::check(!fixture::replies.empty(), "bounded fake exchange replies");
  fixture::sent_markers.push_back(marker);
  fixture::before_ticks.push_back(
      inputs.tick); // exchange caller owns input_lock
  auto next = fixture::replies.front();
  fixture::replies.pop_front();
  return next;
}
} // namespace nx::lan
static void reset(nx::lan::Session &fake, unsigned player) {
  using namespace nx::lan;
  running = false;
  fixture::replies.clear();
  fixture::sent_markers.clear();
  fixture::before_ticks.clear();
  session = &fake;
  role = player;
  count = 2;
  pending_loading_event = 0;
  scheduled_tick = 1;
  exchange_pipeline = nullptr;
  FrameInputs initial;
  initial.players = 2;
  {
    std::lock_guard lock(input_lock);
    fixture::check(input_publication.reset_locked(initial), "initial neutral");
  }
  if (apply_trace)
    delete apply_trace;
  apply_trace = new ApplyTrace(64);
  running = true;
}
static nx::lan::FrameInputs frame(uint64_t tick, uint64_t a, uint64_t b) {
  nx::lan::FrameInputs f;
  f.tick = tick;
  f.players = 2;
  f.loading_events[0] = a;
  f.loading_events[1] = b;
  f.pads[1].buttons = nx::plat::kA;
  return f;
}
int main() {
  using namespace nx::lan;
  using fixture::check;
  const auto phase_env=[](const char* value) {
#ifdef _WIN32
    check(_putenv_s("NX_LAN_TRACE_PHASES",value)==0,"set fixture phase flag");
#else
    check(setenv("NX_LAN_TRACE_PHASES",value,1)==0,"set fixture phase flag");
#endif
  };
  phase_env("1");
  check(!trace_phases_enabled(),"phase flag alone cannot enable trace");
  phase_env("0");
  Session fake;
  const char *path = "fixture:/Sound/Resource/Stream/BGM_CourseFixture.bwav";
  uint64_t marker = 0xcbf29ce484222325ull;
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(path);
       *p; ++p)
    marker = (marker ^ *p) * 0x100000001b3ull;
  nx::CPU cpu{};cpu.x[30]=fixture::main_module.base+0x140;cpu.tpidrro=0xabcdef123456ull;
  fixture::main_info.name="main";fixture::main_info.text=0x100;fixture::main_info.text_size=0x100;
  check(trace_framework_enter(cpu).invocation==0&&fixture::module_queries==0,"default-off observations do not resolve callers");
  trace_loading_file(path,cpu);check(fixture::module_queries==0,"default-off file observation fast path");
  reset(fake, 0);
  const auto observation=trace_framework_enter(cpu);
  check(observation.invocation&&observation.thread,"actual framework entry local identity");
  trace_loading_file(path,cpu);
  const auto hid_snapshot=input_snapshot(1);
  trace_hid_delivery(hid_snapshot,hid_snapshot.pad,cpu);
  fixture::replies = {frame(1, marker, 0), frame(2, marker, marker)};
  loading_event(path);
  trace_framework_leave(cpu,observation);
  check(fixture::replies.empty(), "loader used exactly the bounded replies");
  check(fixture::before_ticks == std::vector<uint64_t>({0, 0}),
        "actual loader does not publish intermediate transport-only tick");
  check(fixture::sent_markers == std::vector<uint64_t>({marker, marker}),
        "actual loader announces its marker");
  check(input_snapshot(1).committed_tick == 2 &&
            input_snapshot(1).pad.buttons == 0,
        "actual loader publishes final neutral only");
  std::ostringstream loader_trace;
  apply_trace->write(loader_trace, 0, 1, true);
  check(loader_trace.str().find("# count_apply=0\n") != std::string::npos &&
            loader_trace.str().find("# count_loading_neutral_apply=1\n") !=
                std::string::npos,
        "actual loader trace labels neutral publication");
  const auto loader_text=loader_trace.str();
  check(loader_text.find("observation_identity=local_invocation_v1")!=std::string::npos&&
        loader_text.find("# count_framework_enter=1\n")!=std::string::npos&&
        loader_text.find("# count_framework_exit=1\n")!=std::string::npos&&
        loader_text.find("# count_loading_file=1\n")!=std::string::npos,"actual entry/file/exit observations exported");
  const auto suffix=","+std::to_string(observation.invocation)+",0,"+std::to_string(observation.thread)+",";
  check(loader_text.find(suffix+std::to_string(marker)+",1,320\n")!=std::string::npos,
        "actual loading file correlates invocation, path hash and source-relative caller");
  check(loader_text.find(path)==std::string::npos&&loader_text.find(std::to_string(cpu.tpidrro))==std::string::npos,
        "no raw path or guest TLS token exported");
  check(loader_text.find("# count_hid_delivery=1\n")!=std::string::npos &&
        loader_text.find(suffix+"0,1,320\n")!=std::string::npos,
        "HID delivery attaches retained invocation, local thread and checked caller");
  auto nested=trace_framework_enter(cpu);trace_framework_leave(cpu,nested,true);
  cpu.x[30]=fixture::main_module.base+0x200;trace_loading_file(path,cpu); // text span excludes upper bound
  cpu.x[30]=fixture::main_module.base+0xff;trace_loading_file(path,cpu); // below nonzero text start
  std::ostringstream unknown_callers;apply_trace->write(unknown_callers,0,1,true);
  check(unknown_callers.str().find(","+std::to_string(marker)+",0,0\n")!=std::string::npos&&
        unknown_callers.str().find("# count_framework_throw=1\n")!=std::string::npos,
        "invalid caller range stays unknown and exceptional exit is distinct");
  check(!trace_phases_enabled()&&!trace_phase_enter(cpu,CalcPhase::Pre).invocation,"trace alone does not enable phases");
  phase_env("true");check(!trace_phases_enabled(),"only exact phase flag1 enables");
  phase_env("1");check(trace_phases_enabled(),"both opt-ins enable phases");
  const auto phase_parent=trace_framework_enter(cpu);
  const auto pre=trace_phase_enter(cpu,CalcPhase::Pre);
  const auto core=trace_phase_enter(cpu,CalcPhase::Core);
  check(pre.parent==phase_parent.invocation&&core.parent==pre.invocation,"real registry preserves nested phase ancestry");
  trace_loading_file(path,cpu);
  trace_phase_leave(cpu,core,CalcPhase::Core,true);
  trace_phase_leave(cpu,pre,CalcPhase::Pre);
  const auto post=trace_phase_enter(cpu,CalcPhase::Post);
  // A changed opt-in after installation must not lose an in-flight exit.
  phase_env("0");trace_phase_leave(cpu,post,CalcPhase::Post);
  trace_framework_leave(cpu,phase_parent);
  std::ostringstream phase_trace;apply_trace->write(phase_trace,0,1,true);
  check(phase_trace.str().find("# count_phase_pre_enter=1\n")!=std::string::npos&&
        phase_trace.str().find("# count_phase_pre_exit=1\n")!=std::string::npos&&
        phase_trace.str().find("# count_phase_core_throw=1\n")!=std::string::npos&&
        phase_trace.str().find("# count_phase_post_exit=1\n")!=std::string::npos,
        "actual source-phase enter/exit/exception export without schema change");
  check(observation_registry.current(cpu.tpidrro).invocation==0,"real phase unwind restores registry");
  phase_env("1");
  {
    // A loader can hold this mutex while waiting for task work. Logging must
    // still forward that task instead of waiting for the loader's mutex.
    std::lock_guard guard(input_lock);
    const auto observed=trace_phase_enter(cpu,CalcPhase::Pre);
    check(observed.invocation!=0,"phase observation succeeds with input mutex held");
    trace_phase_leave(cpu,observed,CalcPhase::Pre);
  }
  phase_env("0");
  reset(fake, 1);
  fixture::replies = {frame(1, marker, 0)};
  nx::lan::next_frame();
  check(fixture::replies.empty(), "ordinary path used exactly one reply");
  const auto exposed = input_snapshot(1);
  check(exposed.committed_tick == 1 && exposed.sampling_number == 2 &&
            exposed.pad.buttons == nx::plat::kA,
        "ordinary framework path exposes remote-loading commit and returns");
  check(fixture::sent_markers == std::vector<uint64_t>({0}),
        "ordinary peer has not reached its local loading marker");
  std::ostringstream ordinary_trace;
  apply_trace->write(ordinary_trace, 1, 1, true);
  check(ordinary_trace.str().find("# count_apply=1\n") != std::string::npos,
        "actual ordinary trace records apply");
  running = false;
  session = nullptr;
  delete apply_trace;
  apply_trace = nullptr;
  std::puts("Actual netplay_game loading/publication characterization passed; "
            "no world-sync claim");
}
