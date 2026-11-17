// Player settings shared by every PC platform: what the F1 menu shows and changes, kept in <data root>/pc-settings.ini
// (the same file and keys as the Win32 menu's, pc_menu_win.cpp, so either menu reads the other's file). Key bindings
// are platform key codes: Windows virtual keys, SDL scancodes on Linux.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace nx::overlay {

constexpr size_t kControlCount = 16;

// The 16 NX actions, in pad bit order of the bindings arrays.
enum Control : size_t {
    CtlA, CtlB, CtlX, CtlY, CtlL, CtlR, CtlZL, CtlZR, CtlPlus, CtlMinus, CtlLStick, CtlRStick,
    CtlLeft, CtlUp, CtlRight, CtlDown,
};

// Physical controller buttons, by position (SDL's south/east/west/north naming).
enum Physical : int {
    PhySouth, PhyEast, PhyWest, PhyNorth, PhyShoulderL, PhyShoulderR, PhyTriggerL, PhyTriggerR,
    PhyStart, PhyBack, PhyStickL, PhyStickR, PhyLeft, PhyUp, PhyRight, PhyDown,
};

struct Settings {
    int width = 1280, height = 720;
    int internal_resolution = 0;  // svc_am.cpp presets: 0 original/dynamic, 720p, 900p, 1080p, 1440p, 2160p, 6 native
    int volume = 100;
    int language = 0;       // menu language: English, Italian, German, French, Spanish, Korean, Japanese, Arabic, Russian, Turkish, Dutch, Simplified Chinese, Traditional Chinese
    int game_language = 0;  // en-US, it, de, fr, pt-BR, es, ko, ja, nl, ru, zh-CN, zh-TW
    int memory_limit_gb = 8;
    bool keyboard_separate_player = false;
    int button_icons = 0;  // Auto, Keyboard, Nintendo, Xbox, PlayStation; applies at startup
    int controller_preset = 1;  // 1 Nintendo positions (the default, as on the Switch: A on the right), 0 Xbox / DualSense
    bool muted = false, fullscreen = false, always_on_top = false;
    bool metrics_enabled = false, low_vram_profile = false;
    bool metric_fps = true, metric_one_percent_low = true, metric_game_speed = true, metric_frametime_graph = true;
    int overlay_position = 0;  // top left, top right, bottom left, bottom right
    int upscaler = 1;          // 0 bilinear, 1 FSR 1 (when the game's image is smaller than the window)
    bool preserve_aspect = true;  // centre the picture with bars instead of stretching non-matching windows
    bool texture_cache = true;  // bounded, byte-verified low-VRAM conversions; can be changed live
    int fsr_sharpness = 25;    // RCAS sharpness in hundredths of a stop: 0 the sharpest, 200 the softest
    int antialiasing = 0;      // 0 off, 1 FXAA (on the presented image)
    int anisotropy = 0;        // 0 the game's own, else 2, 4, 8, 16 (applies after a restart)
    int rumble = 100;          // controller rumble strength, percent (0: off)
    int vsync = 1;             // 0 On (FIFO), 1 Fast (MAILBOX, else IMMEDIATE: the default), 2 Adaptive (FIFO_RELAXED), 3 Off
    int frame_cap = 60;        // 30, 60, 90, 120, or 144 FPS
    bool shader_bundle = true;  // sequential shader cache, applies at startup
    bool framegen = false;  // experimental optical-flow presentation
    int render_ahead = 1;  // maximum queued game pictures: 1 low latency, 2 balanced
    int framegen_multiplier = 2;  // 2..8 displayed frames per game frame
    bool pipeline_skip_draws = false;  // opt-in: hide draws while new pipelines compile (can cause flicker)
    std::array<int, kControlCount> keyboard_bindings{};
    std::array<int, kControlCount> controller_bindings{};
    uint32_t controller_custom_mask = 0;  // bit per action: controller_bindings[action] instead of the preset
};

// The physical button a preset gives an action.
int default_controller_binding(size_t action, int preset);
// The physical button an action is bound to now.
int controller_binding(const Settings& s, size_t action);

enum class GraphicsProfile { Performance, Balanced, Quality };
enum RestartChange : uint32_t {
    RestartResolution = 1 << 0, RestartTextures = 1 << 1, RestartFiltering = 1 << 2,
    RestartShaders = 1 << 3, RestartLanguage = 1 << 4, RestartPrompts = 1 << 5,
};
// Startup-only resources compared with the settings actually used to start this game.
uint32_t restart_changes(const Settings& active, const Settings& requested);
// Change graphics options together, preserving display size, controls, audio, language and user data.
void apply_graphics_profile(Settings& s, GraphicsProfile profile);

// Reads `path` over `s` (missing or invalid keys keep their value). False when there is no file.
bool load_settings(const std::string& path, Settings& s);
void save_settings(const std::string& path, const Settings& s);

}  // namespace nx::overlay
