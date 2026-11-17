#include "controllers.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <nx/runtime.h>
#if defined(_WIN32) || defined(__APPLE__) || (defined(__linux__) && !defined(__ANDROID__))
#define NX_SDL_CONTROLLERS 1
#include <SDL3/SDL.h>
#endif

namespace nx::plat {
namespace {
std::array<std::atomic<int>, 16> bindings{1, 0, 3, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
std::atomic<int> strength{100};
std::atomic<bool> force_primary{false}, separate_requested{false};
constexpr uint64_t bits[] = {kA, kB, kX, kY, kL, kR, kZL, kZR, kPlus, kMinus, kStickL, kStickR,
                             kLeft, kUp, kRight, kDown};
#if NX_SDL_CONTROLLERS
std::mutex lock;
std::array<SDL_Gamepad*, kPlayerCount> pads{};
uint64_t last_scan = 0, last_update = 0;
std::atomic<bool> closing{false};
std::atomic<void (*)(uint32_t)> listener{nullptr};
std::once_flag watcher_started;
std::thread watcher;
bool keyboard_separate = false;
void refresh() {  // caller holds lock; all SDL APIs here are thread-safe
    if (closing) return;
    const uint64_t now = monotonic_ns();
    if (!last_update || now - last_update >= 4'000'000) {
        SDL_UpdateGamepads(); last_update = now;
    }
    for (size_t i = 0; i < pads.size(); ++i)
        if (pads[i] && !SDL_GamepadConnected(pads[i])) {
            log("input: player %zu controller disconnected", i + 1);
            SDL_CloseGamepad(pads[i]); pads[i] = nullptr; last_scan = 0;
        }
    if (last_scan && now - last_scan < 250'000'000) return;
    last_scan = now;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; ids && i < count; ++i) {
        bool assigned = false;
        for (auto* pad : pads) if (pad && SDL_GetGamepadID(pad) == ids[i]) assigned = true;
        if (assigned) continue;
        // Test-process isolation: never open or rumble the user's physical devices.
        if (SDL_getenv("NX_TEST_VIRTUAL_ONLY") && !SDL_IsJoystickVirtual(ids[i])) continue;
        auto slot = std::find(pads.begin() + (keyboard_separate ? 1 : 0), pads.end(), nullptr);
        if (slot == pads.end()) break;
        if (auto* pad = SDL_OpenGamepad(ids[i])) {
            *slot = pad;
            SDL_SetGamepadPlayerIndex(pad, int(slot - pads.begin()));
            log("input: player %zu controller connected: %s", size_t(slot - pads.begin()) + 1, SDL_GetGamepadName(pad));
        }
    }
    SDL_free(ids);
}
#endif
}

void controller_binding(size_t action, int physical) {
    if (action < bindings.size()) bindings[action] = std::clamp(physical, 0, 15);
}
int current_controller_binding(size_t action) {
    return action < bindings.size() ? bindings[action].load(std::memory_order_relaxed) : -1;
}
void controller_rumble_strength(int percent) { strength = std::clamp(percent, 0, 100); }
void controller_keyboard_separate(bool separate) {
    separate_requested=separate;
    separate=separate && !force_primary.load();
#if NX_SDL_CONTROLLERS
    std::lock_guard guard(lock);
    if (keyboard_separate == separate) return;
    keyboard_separate = separate;
    for (auto*& pad : pads) if (pad) { SDL_CloseGamepad(pad); pad = nullptr; }
    last_scan = 0;
    refresh();
#endif
}
void controller_force_primary(bool force) {
    force_primary=force;
    controller_keyboard_separate(separate_requested.load());
}

bool read_controller(uint32_t player, PadState& out, std::array<bool, 16>* physical) {
    out = {};
    if (physical) physical->fill(false);
    if (player >= kPlayerCount) return false;
#if NX_SDL_CONTROLLERS
    std::lock_guard guard(lock);
    refresh();
    auto* pad = pads[player];
    if (!pad) return false;
    constexpr SDL_GamepadButton buttons[] = {
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
        SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_INVALID,
        SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
        SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_DOWN};
    std::array<bool, 16> down{};
    for (size_t i = 0; i < down.size(); ++i)
        down[i] = buttons[i] != SDL_GAMEPAD_BUTTON_INVALID && SDL_GetGamepadButton(pad, buttons[i]);
    down[6] = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 10000;
    down[7] = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 10000;
    if (physical) *physical = down;
    for (size_t i = 0; i < bindings.size(); ++i) if (down[size_t(bindings[i].load())]) out.buttons |= bits[i];
    auto axis = [&](SDL_GamepadAxis a, bool invert) {
        int v = SDL_GetGamepadAxis(pad, a);
        v = v > -7849 && v < 7849 ? 0 : std::clamp(v, -kStickMax, kStickMax);
        return invert ? -v : v;
    };
    out.lx = axis(SDL_GAMEPAD_AXIS_LEFTX, false); out.ly = axis(SDL_GAMEPAD_AXIS_LEFTY, true);
    out.rx = axis(SDL_GAMEPAD_AXIS_RIGHTX, false); out.ry = axis(SDL_GAMEPAD_AXIS_RIGHTY, true);
    return true;
#else
    return false;
#endif
}

bool player_connected(uint32_t player) {
    if (player == 0) return true;
    PadState unused;
    return read_controller(player, unused);
}
bool read_player_pad(uint32_t player, PadState& out) {
    if (player == 0) { read_pad(out); return true; }
    const bool connected = read_controller(player, out);
    if (connected) input_script(out, player);
    return connected;
}

void rumble_player(uint32_t player, float low, float high) {
    if (player >= kPlayerCount) return;
#if NX_SDL_CONTROLLERS
    std::lock_guard guard(lock);
    if (!pads[player]) return;
    const float k = float(strength.load()) / 100.0f;
    SDL_RumbleGamepad(pads[player], uint16_t(std::clamp(low * k, 0.0f, 1.0f) * 65535),
                     uint16_t(std::clamp(high * k, 0.0f, 1.0f) * 65535), 250);
#else
    if (player == 0) rumble(low, high);
#endif
}

ControllerKind controller_kind(uint32_t player) {
#if NX_SDL_CONTROLLERS
    if (player >= kPlayerCount) return ControllerKind::Keyboard;
    std::lock_guard guard(lock); refresh();
    if (!pads[player]) return ControllerKind::Keyboard;
    const auto label = SDL_GetGamepadButtonLabel(pads[player], SDL_GAMEPAD_BUTTON_SOUTH);
    if (label == SDL_GAMEPAD_BUTTON_LABEL_CROSS) return ControllerKind::PlayStation;
    if (label == SDL_GAMEPAD_BUTTON_LABEL_B) return ControllerKind::Nintendo;
    return ControllerKind::Xbox;
#else
    return ControllerKind::Keyboard;
#endif
}
std::string controller_name(uint32_t player) {
#if NX_SDL_CONTROLLERS
    if (player >= kPlayerCount) return "Not connected";
    std::lock_guard guard(lock); refresh();
    if (pads[player]) { const char* name = SDL_GetGamepadName(pads[player]); return name ? name : "Controller"; }
#endif
    return player == 0 ? "Keyboard" : "Not connected";
}
void close_controllers() {
#if NX_SDL_CONTROLLERS
    closing = true;
    if (watcher.joinable()) watcher.join();
    std::lock_guard guard(lock);
    for (auto*& pad : pads) if (pad) { SDL_CloseGamepad(pad); pad = nullptr; }
    last_scan = last_update = 0;
#endif
}
void watch_controller_connections(void (*changed)(uint32_t)) {
#if NX_SDL_CONTROLLERS
    listener = changed;
    std::call_once(watcher_started, [] {
        std::array<bool, kPlayerCount> initial{};
        for (uint32_t i = 0; i < kPlayerCount; ++i) initial[i] = player_connected(i);
        watcher = std::thread([previous = initial]() mutable {
            while (!closing) {
                for (uint32_t player = 1; player < kPlayerCount; ++player) {
                    const bool connected = player_connected(player);
                    if (connected != previous[player]) {
                        previous[player] = connected;
                        if (auto fn = listener.load()) fn(player);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    });
#endif
}
}
