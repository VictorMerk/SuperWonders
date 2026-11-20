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
  Session fake;
  const char *path = "fixture-loading-event";
  uint64_t marker = 0xcbf29ce484222325ull;
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(path);
       *p; ++p)
    marker = (marker ^ *p) * 0x100000001b3ull;
  reset(fake, 0);
  fixture::replies = {frame(1, marker, 0), frame(2, marker, marker)};
  loading_event(path);
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
