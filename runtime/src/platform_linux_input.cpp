// Linux input and player settings: the first SDL gamepad and the keyboard through the settings' bindings, the
// pc-settings.ini settings applied (window, volume, bindings, ...), and keys and mouse handed to the overlay menu
// (overlay/). Split from platform_linux.cpp (700-line rule).
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

#include <SDL3/SDL.h>

#include <nx/runtime.h>

#include "nvn/nvn_vk.h"
#include "overlay/overlay.h"
#include "frametrace.h"
#include "overlay/settings.h"
#include "platform.h"
#include "stream_cache.h"
#include "controllers.h"
#include "keyboard_actions.h"
#include "platform_linux.h"
#include "svc_am.h"

namespace nx::plat {

namespace {

using linux_host::g_stream;
using linux_host::g_window;

std::mutex g_pad_lock;
PadState g_pad;

// Player settings (overlay/settings.h). Keyboard bindings are SDL scancodes. g_settings_lock guards g_settings; the
// bindings are copied into atomics for the input sampling.
std::mutex g_settings_lock;
overlay::Settings g_settings;
std::array<std::atomic<int>, overlay::kControlCount> g_key_binding{}, g_pad_binding{};
std::atomic<bool> g_keyboard_actions{true};std::atomic<int> g_keyboard_game_layout{0},g_keyboard_standee{25};
constexpr uint64_t kActionBits[overlay::kControlCount] = {
    kA, kB, kX, kY, kL, kR, kZL, kZR, kPlus, kMinus, kStickL, kStickR, kLeft, kUp, kRight, kDown,
};

std::atomic<bool> g_fast_forward{false};  // Tab held (sampled on the main thread)
// The open gamepad: sampled on the main thread, rumbled from the game's (both under g_gamepad_lock).
std::mutex g_gamepad_lock;
SDL_Gamepad* g_gamepad = nullptr;
std::atomic<int> g_rumble_strength{100};  // percent

std::mutex g_posted_lock;
std::deque<std::function<void()>> g_posted;

void post_to_main(std::function<void()> fn) {  // runs on the main thread soon; does not wait
    std::lock_guard lk(g_posted_lock);
    g_posted.push_back(std::move(fn));
}

std::string settings_path() { return data_root() + "/pc-settings.ini"; }

void apply_settings(const overlay::Settings& s, bool persist) {
    overlay::Settings before;
    {
        std::lock_guard lk(g_settings_lock);
        before = g_settings;
        g_settings = s;
    }
    for (size_t a = 0; a < overlay::kControlCount; ++a) {
        g_key_binding[a] = s.keyboard_bindings[a], g_pad_binding[a] = overlay::controller_binding(s, a);
        controller_binding(a, overlay::controller_binding(s, a));
    }
    g_keyboard_actions=s.keyboard_actions;g_keyboard_game_layout=s.keyboard_game_layout;g_keyboard_standee=s.keyboard_standee_key;
    controller_rumble_strength(s.rumble);
    controller_keyboard_separate(s.keyboard_separate_player);
    if (g_stream) {
        const int percent = std::clamp(s.volume, 0, 200);
        const float gain = percent <= 100 ? (float(percent) / 100.0f) * (float(percent) / 100.0f)
                                          : float(percent) / 100.0f;
        SDL_SetAudioStreamGain(g_stream, s.muted ? 0.0f : gain);
    }
    nx::set_internal_resolution_preset(s.internal_resolution);  // read at the next boot
    nx::set_game_language(s.game_language);
    set_memory_limit_gb(s.memory_limit_gb);
    setenv("NX_LOW_VRAM_PROFILE", s.low_vram_profile ? "1" : "0", 1);
    nvn::set_fsr(s.upscaler == 1, float(s.fsr_sharpness) / 100.0f);
    nvn::set_fxaa(s.antialiasing == 1);
    nvn::set_anisotropy(s.anisotropy);
    nvn::set_vsync(s.vsync);
    nvn::set_frame_cap(s.frame_cap);
    nvn::set_pipeline_skip_draws(s.pipeline_skip_draws);
    nvn::set_framegen(s.framegen, s.framegen_multiplier);
    nvn::set_render_ahead(s.render_ahead);
    nvn::set_preserve_aspect(s.preserve_aspect);
    nvn::set_texture_cache(s.texture_cache);
    streaming::set_enabled(s.preload_music);
    g_rumble_strength = std::clamp(s.rumble, 0, 100);
    if (s.fullscreen != before.fullscreen || s.width != before.width || s.height != before.height ||
        s.always_on_top != before.always_on_top)
        post_to_main([s] {
            if (!g_window) return;
            SDL_SetWindowFullscreen(g_window, s.fullscreen);
            if (!s.fullscreen) SDL_SetWindowSize(g_window, s.width, s.height);
            SDL_SetWindowAlwaysOnTop(g_window, s.always_on_top);
        });
    if (persist) overlay::save_settings(settings_path(), s);
}

const char* key_name(int scancode) {
    const char* n = SDL_GetScancodeName(SDL_Scancode(scancode));
    return n && *n ? n : "-";
}

// Keys the overlay understands, from SDL keycodes.
overlay::Key menu_key(SDL_Keycode k) {
    switch (k) {
        case SDLK_TAB: return overlay::KeyTab;
        case SDLK_LEFT: return overlay::KeyLeft;
        case SDLK_RIGHT: return overlay::KeyRight;
        case SDLK_UP: return overlay::KeyUp;
        case SDLK_DOWN: return overlay::KeyDown;
        case SDLK_PAGEUP: return overlay::KeyPageUp;
        case SDLK_PAGEDOWN: return overlay::KeyPageDown;
        case SDLK_HOME: return overlay::KeyHome;
        case SDLK_END: return overlay::KeyEnd;
        case SDLK_RETURN: case SDLK_KP_ENTER: return overlay::KeyEnter;
        case SDLK_ESCAPE: return overlay::KeyEscape;
        case SDLK_BACKSPACE: return overlay::KeyBackspace;
        case SDLK_DELETE: return overlay::KeyDelete;
        case SDLK_SPACE: return overlay::KeySpace;
        case SDLK_LSHIFT: case SDLK_RSHIFT: return overlay::KeyShift;
        case SDLK_LCTRL: case SDLK_RCTRL: return overlay::KeyCtrl;
        case SDLK_F1: return overlay::KeyF1;
        default: return overlay::KeyNone;
    }
}

}  // namespace

bool fast_forward() { return g_fast_forward.load(std::memory_order_relaxed); }

void rumble(float low, float high) {
    rumble_player(0, low, high);
}

void read_pad(PadState& p) {
    ft::CallTimer timing(ft::TimedCall::InputPoll);
    {
        std::lock_guard lk(g_pad_lock);
        p.buttons |= g_pad.buttons;
        if (g_pad.lx) p.lx = g_pad.lx;
        if (g_pad.ly) p.ly = g_pad.ly;
        if (g_pad.rx) p.rx = g_pad.rx;
        if (g_pad.ry) p.ry = g_pad.ry;
    }
    input_script(p);
    overlay::filter_pad(p);  // controller navigation of the menu; the game sees nothing while it is open
}

namespace linux_host {

void initial_window(int& w, int& h, bool& fullscreen) {
    std::lock_guard lk(g_settings_lock);
    w = g_settings.width, h = g_settings.height, fullscreen = g_settings.fullscreen;
}

void load_player_settings() {
    overlay::Settings s;
    for (size_t a = 0; a < overlay::kControlCount; ++a)
        s.controller_bindings[a] = overlay::default_controller_binding(a, s.controller_preset);
    bool found = overlay::load_settings(settings_path(), s);
    // Aspect-dependent resources and render targets are selected at startup. Keep explicit environment overrides
    // for developer tests, otherwise configure the same profiles as the Windows launcher.
    const char* profile = std::getenv("NX_ASPECT_PROFILE");
    int ratio = profile && std::strcmp(profile, "21:9") == 0 ? 1 :
                profile && std::strcmp(profile, "32:9") == 0 ? 2 :
                profile && std::strcmp(profile, "16:9") == 0 ? 0 : std::clamp(s.aspect_ratio, 0, 2);
    setenv("NX_ASPECT_PROFILE", ratio == 1 ? "21:9" : ratio == 2 ? "32:9" : "16:9", 0);
    setenv("NX_ULTRAWIDE", ratio == 1 ? "2560x1080" : ratio == 2 ? "3840x1080" : "0", 0);
    const char* flags[] = {"NX_TERRAIN_MOD_ASPECT_TEST", "NX_UW_POWERUP_TEST", "NX_UW_FLOWER_TEST",
        "NX_UW_MAP_ALIGN_TEST", "NX_UW_MAP_COMPOSITE_TEST", "NX_UW_MAP_CARD_TEST",
        "NX_UW_MAP_MARKERS_TEST", "NX_UW_MAP_DETAIL_TEST", "NX_UW_SETTINGS_SCISSOR_TEST"};
    for (const char* flag : flags) setenv(flag, ratio ? "1" : "0", 0);
    // The alternate flower offset shifts normal Linux bubbles away from their flower.
    setenv("NX_UW_FLOWER_ALT_TEST", "0", 0);
    if (ratio > 0 && s.width > 0) s.height = std::max(360, ratio == 1 ? s.width * 27 / 64 :
                                              ratio == 2 ? s.width * 9 / 32 : s.width * 9 / 16);
    if (const char* e = std::getenv("NX_MEM_LIMIT_GB"); e && std::atof(e) > 0) s.memory_limit_gb = std::atoi(e);
    apply_settings(s, false);
    overlay::Host host;
    host.apply = apply_settings;
    host.key_name = key_name;
    // Wayland has no "always on top" for clients; X11 does.
    const char* driver = SDL_GetCurrentVideoDriver();
    host.can_always_on_top = !driver || std::strcmp(driver, "wayland") != 0;
    overlay::init(host, s);
    log("platform: settings %s (%s)", found ? "loaded" : "defaults", settings_path().c_str());
}

void run_posted() {
    std::deque<std::function<void()>> posted;
    {
        std::lock_guard lk(g_posted_lock);
        posted.swap(g_posted);
    }
    for (auto& fn : posted) fn();
}

bool handle_event(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            const bool down = e.type == SDL_EVENT_KEY_DOWN;
            if (down && e.key.scancode == SDL_SCANCODE_F11 && g_window) {
                bool full = !(SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN);
                SDL_SetWindowFullscreen(g_window, full);
                std::lock_guard lk(g_settings_lock);
                g_settings.fullscreen = full;
                overlay::sync(g_settings);
            }
            if (overlay::enabled() && !e.key.repeat) overlay::key_event(int(e.key.scancode), menu_key(e.key.key), down);
            return true;
        }
        case SDL_EVENT_MOUSE_MOTION:
            if (overlay::wants_mouse() && g_window) {
                float d = SDL_GetWindowPixelDensity(g_window);
                overlay::mouse_move(e.motion.x * d, e.motion.y * d);
            }
            return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (overlay::wants_mouse()) {
                int b = e.button.button == SDL_BUTTON_LEFT ? 0 : e.button.button == SDL_BUTTON_RIGHT ? 1 : 2;
                overlay::mouse_button(b, e.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            }
            return true;
        case SDL_EVENT_MOUSE_WHEEL:
            if (overlay::wants_mouse()) overlay::mouse_wheel(e.wheel.y);
            return true;
        default:
            return false;
    }
}

void sample_input() {
    PadState p;
    read_controller(0, p);
    // Named PC actions are translated before LAN sampling and without changing gamepad input.
    static input::KeyboardActionState keyboard_state;
    bool tab = false;
    if (g_window && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_INPUT_FOCUS) && !overlay::wants_keyboard()) {
        int count=0;const bool* k=SDL_GetKeyboardState(&count);tab=k[SDL_SCANCODE_TAB];
        const bool actions=g_keyboard_actions.load();input::KeyboardBindings bindings;
        for(size_t a=0;a<bindings.size();++a)bindings[a]=g_key_binding[a].load();
        const auto keyboard=input::keyboard_pad(bindings,actions,g_keyboard_game_layout.load(),g_keyboard_standee.load(),input::KeyboardCodes::SDL,keyboard_state,monotonic_ns(),
            [&](int sc){return sc>0 && sc<count && k[sc] && !(sc==SDL_SCANCODE_ESCAPE && overlay::enabled());});
        p.buttons|=keyboard.buttons;if(keyboard.lx)p.lx=keyboard.lx;if(keyboard.ly)p.ly=keyboard.ly;
        if(!actions) {
            if(k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT])p.lx=-kStickMax;
            if(k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT])p.lx=kStickMax;
            if(k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP])p.ly=kStickMax;
            if(k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN])p.ly=-kStickMax;
        }
    }
    else keyboard_state={};
    g_fast_forward = tab;
    std::lock_guard lk(g_pad_lock);
    g_pad = p;
}

}  // namespace linux_host

}  // namespace nx::plat
