// Exercise the production HLE implementation, not a parallel copy of HID logic.
// Registrations and external host/LAN boundaries below belong only to this TU;
// no game, device, network, window or Vulkan API is initialized. Vulkan headers
// are required transitively by overlay.h, but no Vulkan library is linked.
#include "ipc.h"
#include "netplay_game.h"
#include <cstdio>
#include <cstdlib>
#include <nx/runtime.h>
namespace nx {
std::atomic<uint64_t> g_frame{0};
void log(const char *, ...) {}
const char *hle_current_name() { return "HID regression"; }
std::string describe(uint64_t) { return "test"; }
void register_service(const std::string &, ServiceFactory) {}
uint32_t Service::handle(IpcCtx &) { return 0; }
namespace lan {
InputPublication publication;
bool network = true;
uint64_t loading = 0;
InputSnapshot observed;
plat::PadState delivered;
size_t traces = 0;
uint64_t observed_caller=0,observed_thread=0;
bool active() { return network; }
uint32_t local_player() { return 0; }
InputSnapshot input_snapshot(uint32_t p) {
  return publication.read(p,
                          [] { return InputReadMetadata{network, loading}; });
}
void trace_hid_delivery(const InputSnapshot &s, const plat::PadState &p,
                        const CPU& cpu) noexcept {
  observed_caller = cpu.x[30];
  observed_thread = cpu.tpidrro;
  observed = s;
  delivered = p;
  ++traces;
}
} // namespace lan
namespace plat {
PadState local;
bool local_connected[4] = {true, true, false, false};
void read_pad(PadState &p) { p = local; }
void rumble_player(uint32_t, float, float) {}
bool player_connected(uint32_t p) { return p < 4 && local_connected[p]; }
bool read_player_pad(uint32_t p, PadState &out) {
  out = local;
  return player_connected(p);
}
} // namespace plat
namespace play {
void pad(plat::PadState &) {}
} // namespace play
namespace ft {
void input_sample(uint64_t) {}
} // namespace ft
} // namespace nx
#undef NX_HLE
#define NX_HLE(sym, fn)
// Including the implementation gives access to its private ABI/history
// handlers. GNU-style section GC discards unrelated handlers and their host
// dependencies.
#include "hle_hid.cpp"
static void check(bool ok, const char *why) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", why);
    std::exit(1);
  }
}
struct State {
  int64_t sample;
  uint64_t buttons;
  int32_t lx, ly, rx, ry;
  uint32_t attributes, padding;
};
static_assert(sizeof(State) == 0x28);
static State one(uint32_t id) {
  State s{};
  nx::CPU c{};
  c.x[30]=0x123456; c.tpidrro=0xabcdef;
  c.x[0] = reinterpret_cast<uintptr_t>(&s);
  c.x[1] = reinterpret_cast<uintptr_t>(&id);
  nx::GetNpadState(c);
  if(nx::lan::network && id<2) {
    check(nx::lan::observed_caller==c.x[30] && nx::lan::observed_thread==c.tpidrro,"HID forwards real calling CPU identity");
  }
  return s;
}
static size_t history(uint32_t id, std::array<State, 16> &s, int n = 16) {
  nx::CPU c{};
  c.x[30]=0x123456; c.tpidrro=0xabcdef;
  c.x[0] = reinterpret_cast<uintptr_t>(s.data());
  c.x[1] = uint64_t(n);
  c.x[2] = reinterpret_cast<uintptr_t>(&id);
  nx::GetNpadStatesFullKey(c);
  return c.x[0];
}
static void publish(uint64_t tick, bool neutral = false, uint8_t players = 2) {
  nx::lan::FrameInputs f;
  f.tick = tick;
  f.players = players;
  if (!neutral) {
    f.pads[0] = {nx::plat::kA, -30000, 30000, 30000, -30000};
    f.pads[1] = {nx::plat::kB, 0, 0, 0, 0};
  }
  std::lock_guard l(nx::lan::publication.mutex());
  check(nx::lan::publication.publish_locked(f), "commit publication");
}
int main() {
  using namespace nx;
  std::array<State, 16> h{};
  publish(0);
  g_frame = 50;
  auto s = one(0);
  const uint64_t bits = plat::kA | plat::kStickLLeft | plat::kStickLUp |
                        plat::kStickRRight | plat::kStickRDown;
  check(s.sample == 1 && s.buttons == bits && s.attributes == 1 &&
            s.lx == -30000 && s.ry == -30000,
        "guest ABI + derived analog buttons");
  check(lan::observed.committed_tick == 0 &&
            lan::observed.sampling_number == 1 &&
            lan::delivered.buttons == bits,
        "actual trace captures commit and delivered buttons");
  check(history(0, h) == 1 && h[0].sample == 1, "initial history");
  for (int i = 0; i < 20; ++i)
    check(one(0).sample == 1 && history(0, h) == 1 && h[1].sample == 0,
          "repeat does not manufacture history");
  g_frame = 999;
  check(one(0).sample == 1 && history(0, h) == 1,
        "presented frame change is not publication");
  publish(1);
  check(one(0).sample == 2 && history(0, h) == 2 && h[1].sample == 1,
        "new commit at unchanged presented frame");
  lan::loading = 17;
  publish(2, true);
  s = one(0);
  check(s.sample == 3 && s.buttons == 0 && s.lx == 0 && s.attributes == 1,
        "final loading neutral ABI");
  check(lan::observed.local_loading_event == 17 &&
            lan::observed.committed_tick == 2,
        "loading marker atomic metadata");
  check(history(0, h) == 3 && h[1].sample == 2 && h[2].sample == 1,
        "loading history identity");
  size_t traces = lan::traces;
  for (uint32_t id : {2u, 4u, 0xffffffffu}) {
    s = one(id);
    check(s.sample == 3 && s.buttons == 0 && s.attributes == 0,
          "disconnected/invalid guest ABI");
    check(history(id, h) == 1 && h[0].sample == 3 && h[1].sample == 0,
          "disconnected history");
  }
  check(lan::traces == traces, "invalid reads have no delivered trace");
  check(history(1, h) == 1 && h[0].attributes == 1, "separate player history");
  check(lan::observed.player == 1 && lan::observed.players == 2 &&
            lan::observed.sampling_number == 3,
        "trace identity and control metadata belong to the delivered player");
  for (uint64_t t = 3; t < 24; ++t) {
    publish(t);
    history(0, h);
  }
  check(h[0].sample == 24 && h[15].sample == 9, "history bounded newest first");
  check(history(0, h, 0) == 0, "zero request");
  publish(24, false, 1);
  check(history(1, h) == 1 && h[0].sample == 25 && !h[0].attributes &&
            h[1].sample == 0,
        "disconnect clears retained player history");
  publish(25);
  check(history(1, h) == 1 && h[0].sample == 26 && h[0].attributes == 1,
        "reconnection starts a fresh history");
  lan::network = false;
  plat::local = {plat::kX, 0, 0, 0, 0};
  s = one(0);
  auto next = one(0);
  check(s.sample > 0 && next.sample == s.sample + 1 && next.buttons == plat::kX,
        "nonLAN per-read sampling preserved");
  size_t before = history(1, h);
  size_t after = history(1, h);
  check(after == before + 1 && h[0].sample == h[1].sample + 1,
        "nonLAN appends every read");
  check(one(3).attributes == 0, "nonLAN disconnected");
  std::puts("actual HLE HID ABI/history regression passed");
}
