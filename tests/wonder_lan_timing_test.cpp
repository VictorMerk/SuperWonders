// Exercise actual Wonder timing branches against a fake aligned runtime page.
// No guest executable/assets, GPU, window, Steam client or network is loaded.
// Unrelated loader/game hooks fail closed. Section GC keeps this CPU-only.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <nx/gamelib.h>
#include <nx/runtime.h>
namespace fixture {
alignas(4096) std::array<unsigned char, 8192> memory{};
nx::ModuleInfo module{};
nx::GameLib library{};
bool lan = true;
uint32_t rate = 60;
unsigned guard = 0, pumps = 0;
} // namespace fixture
namespace nx {
const GameLib &game_lib() { return fixture::library; }
const LoadedModule *module_at(uint64_t) { std::abort(); }
void log(const char *, ...) {}
namespace lan {
bool active() { return fixture::lan; }
uint32_t simulation_rate() { return fixture::rate; }
void loading_event(const char *) { std::abort(); }
} // namespace lan
namespace plat {
std::string setting(const std::string &) { return ""; }
void window_size(uint32_t &w, uint32_t &h) {
  w = 1280;
  h = 720;
}
void overlay_game_speed(double) {}
} // namespace plat
namespace steam_dev {
void pump_current() { ++fixture::pumps; }
} // namespace steam_dev
namespace wonder {
void seed_guard_tick(uint64_t) { ++fixture::guard; }
void menu_init() { std::abort(); }
void lan_init() { std::abort(); }
void seed_guard_init() { std::abort(); }
} // namespace wonder
namespace nvn {
int held_frame_cap() { return 0; }
void hold_scene_frame_cap(int) { std::abort(); }
float uw_factor() { std::abort(); }
} // namespace nvn
} // namespace nx
#include "game.cpp" // production implementation, including private initialization
static void check(bool b, const char *m) {
  if (!b) {
    std::fprintf(stderr, "%s\n", m);
    std::abort();
  }
}
int main() {
  using namespace nx;
  // This process-only setting makes the offline branch independent of the
  // developer shell. LAN must bypass it and use the agreed rate regardless.
#if defined(_WIN32)
  check(_putenv_s("NX_REALTIME_FRAME_STEP", "1") == 0, "set fixture timing mode");
#else
  check(setenv("NX_REALTIME_FRAME_STEP", "1", 1) == 0, "set fixture timing mode");
#endif
  fixture::module.base = reinterpret_cast<uintptr_t>(fixture::memory.data());
  fixture::module.frame_step_offset = 4096;
  fixture::library.main = &fixture::module;
  unsigned presents = 0;
  const auto fixed_step = [](uint32_t rate) {
    float pair[2];
    std::memcpy(pair, fixture::memory.data() + 4096, sizeof(pair));
    check(pair[1] == 1.f / float(rate) && pair[0] == pair[1] * 60.f,
          "LAN step must remain exactly host rate despite wall clock/interval");
  };
  uint64_t now = 1;
  for (uint32_t rate : {30u, 60u}) {
    fixture::rate = rate;
    init_frame_step();
    fixed_step(rate); // initialization must be correct before the first present
    for (unsigned i = 0; i < 70; ++i) {
      constexpr uint64_t gaps[] = {500000, 3000000, 45000000, 100000000};
      now += i == 35 ? 30000000000ull : gaps[i % 4];
      game::frame_presented(now, 7); // deliberately conflicts with agreed30/60
      ++presents;
      fixed_step(rate); // includes the wall-clock game-speed reporting branch
    }
  }
  check(fixture::guard == 0, "LAN branch must not call realtime guard");
  fixture::lan = false;
  game::frame_presented(now + 1000000000ull, 1);
  game::frame_presented(now + 1100000000ull, 1);
  float seconds;
  std::memcpy(&seconds, fixture::memory.data() + 4100, 4);
  check(seconds == float(1. / 15.) && fixture::guard == 1,
        "nonLAN adaptive clamp remains independent");
  check(fixture::pumps == presents + 2, "normal callback pump preserved");
  std::puts("actual Wonder LAN fixed/nonLAN adaptive timing branches passed");
}
