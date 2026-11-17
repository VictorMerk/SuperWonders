// Exercise the actual SDL backend with four virtual gamepads, without game files or physical hardware.
#include "controllers.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace nx { void log(const char*, ...) {} }
namespace nx::plat {
uint64_t monotonic_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void read_pad(PadState& p) { p = {}; }
void input_script(PadState&, uint32_t) {}
}
static void check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s (%s)\n", message, SDL_GetError()); std::exit(1); }
}
static std::atomic<uint32_t> changed{0};
static void connection(uint32_t player) { changed.fetch_or(1u << player); }
struct Motors { std::atomic<uint16_t> low{0}, high{0}; } motors[4];
static bool SDLCALL rumble(void* data, Uint16 low, Uint16 high) {
    auto& m = *static_cast<Motors*>(data); m.low = low; m.high = high; return true;
}

int main() {
    using namespace nx::plat;
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "NX_TEST_VIRTUAL_ONLY", "1", true);
    check(SDL_Init(SDL_INIT_GAMEPAD), "SDL gamepad initialization");
    std::array<SDL_JoystickID, 4> ids{};
    std::array<SDL_Joystick*, 4> joysticks{};
    auto attach = [&](int player) {
        SDL_VirtualJoystickDesc desc; SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD; desc.name = "SuperWonders regression controller";
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT; desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
        desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
        desc.userdata = &motors[player]; desc.Rumble = rumble;
        ids[player] = SDL_AttachVirtualJoystick(&desc);
        check(ids[player] != 0, "attach virtual controller");
        joysticks[player] = SDL_OpenJoystick(ids[player]); check(joysticks[player], "open virtual joystick");
    };
    for (int i = 0; i < 4; ++i) { attach(i); controller_binding(size_t(i), i); }
    for (int i = 4; i < 16; ++i) controller_binding(size_t(i), i);
    for (int i = 0; i < 4; ++i) check(SDL_SetJoystickVirtualButton(joysticks[i], i, true), "set independent button");
    check(SDL_SetJoystickVirtualAxis(joysticks[3], SDL_GAMEPAD_AXIS_LEFTY, -32768), "set stick axis");
    SDL_UpdateGamepads();
    const uint64_t expected[] = {kA, kB, kX, kY};
    for (uint32_t i = 0; i < 4; ++i) {
        PadState p;
        check(read_controller(i, p), "all four players connect");
        check(p.buttons == expected[i], "player inputs must not leak between slots");
        if (i == 3) check(p.ly == kStickMax, "axis direction and -32768 clamping");
    }
    controller_rumble_strength(50); rumble_player(2, 1, 0.5f);
    check(motors[2].low > 0 && motors[0].low == 0 && motors[1].low == 0 && motors[3].low == 0,
          "rumble reaches only the requested player");
    watch_controller_connections(connection);
    SDL_CloseJoystick(joysticks[1]); joysticks[1] = nullptr;
    check(SDL_DetachVirtualJoystick(ids[1]), "disconnect player two");
    const uint64_t deadline = monotonic_ns() + 2'000'000'000;
    while (!(changed.load() & 2u) && monotonic_ns() < deadline) SDL_Delay(10);
    PadState p;
    check(!read_controller(1, p) && p.buttons == 0, "disconnected player releases input");
    check(read_controller(2, p) && p.buttons == kX, "remaining players keep their slots");
    check(changed.load() & 2u, "hotplug observer receives connection changes");
    attach(1);
    const uint64_t reconnect_deadline = monotonic_ns() + 2'000'000'000;
    while (!read_controller(1, p) && monotonic_ns() < reconnect_deadline) SDL_Delay(10);
    check(read_controller(1, p), "replacement reuses the vacant slot");
    controller_keyboard_separate(true);
    check(!read_controller(0, p), "keyboard player does not share the first gamepad");
    check(player_connected(0), "keyboard player remains connected");
    check(read_controller(1, p) && p.buttons == kA, "first gamepad becomes player two");
    check(read_controller(3, p) && p.buttons == kY, "third enumerated gamepad becomes player four");
    controller_force_primary(true);
    check(read_controller(0,p) && p.buttons==kA,"LAN always samples the primary local gamepad");
    controller_keyboard_separate(true);
    check(read_controller(0,p) && p.buttons==kA,"applying saved settings cannot move the LAN controller to another slot");
    controller_force_primary(false);
    check(!read_controller(0,p) && read_controller(1,p),"leaving a primary-device override restores the local-co-op preference");
    controller_keyboard_separate(false);
    check(read_controller(0, p) && p.buttons == kA, "four-gamepad mode can be restored");
    close_controllers();
    for (int i = 0; i < 4; ++i) { if (joysticks[i]) SDL_CloseJoystick(joysticks[i]); SDL_DetachVirtualJoystick(ids[i]); }
    SDL_Quit();
    std::puts("four-player isolation, axes, rumble and hotplug tests passed");
}
