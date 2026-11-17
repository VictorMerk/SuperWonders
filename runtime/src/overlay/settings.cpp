#include "settings.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace nx::overlay {

uint32_t restart_changes(const Settings& active, const Settings& requested) {
    uint32_t changes = 0;
    if (active.internal_resolution != requested.internal_resolution) changes |= RestartResolution;
    if (active.low_vram_profile != requested.low_vram_profile) changes |= RestartTextures;
    if (active.anisotropy != requested.anisotropy) changes |= RestartFiltering;
    if (active.shader_bundle != requested.shader_bundle) changes |= RestartShaders;
    if (active.game_language != requested.game_language) changes |= RestartLanguage;
    if (active.button_icons != requested.button_icons || active.keyboard_bindings != requested.keyboard_bindings)
        changes |= RestartPrompts;
    for (size_t action = 0; action < kControlCount; ++action)
        if (controller_binding(active, action) != controller_binding(requested, action)) changes |= RestartPrompts;
    return changes;
}

void apply_graphics_profile(Settings& s, GraphicsProfile profile) {
    s.frame_cap = 60;
    s.vsync = 1;
    s.pipeline_skip_draws = false;
    s.framegen = false;
    s.upscaler = 1;
    s.internal_resolution = profile == GraphicsProfile::Performance ? 1 : profile == GraphicsProfile::Balanced ? 2 : 3;
    s.low_vram_profile = profile == GraphicsProfile::Performance;
    s.antialiasing = profile == GraphicsProfile::Performance ? 0 : 1;
    s.anisotropy = profile == GraphicsProfile::Performance ? 0 : profile == GraphicsProfile::Balanced ? 4 : 8;
    s.fsr_sharpness = profile == GraphicsProfile::Quality ? 50 : 100;
}

int default_controller_binding(size_t action, int preset) {
    constexpr int standard[kControlCount] = {
        PhySouth, PhyEast, PhyWest, PhyNorth, PhyShoulderL, PhyShoulderR, PhyTriggerL, PhyTriggerR,
        PhyStart, PhyBack, PhyStickL, PhyStickR, PhyLeft, PhyUp, PhyRight, PhyDown,
    };
    constexpr int nintendo[kControlCount] = {
        PhyEast, PhySouth, PhyNorth, PhyWest, PhyShoulderL, PhyShoulderR, PhyTriggerL, PhyTriggerR,
        PhyStart, PhyBack, PhyStickL, PhyStickR, PhyLeft, PhyUp, PhyRight, PhyDown,
    };
    // Circle confirms / Cross backs out, with Square and Triangle unchanged.
    if (preset == 2 && action < 2) return action == 0 ? PhyEast : PhySouth;
    return action < kControlCount ? (preset == 1 ? nintendo[action] : standard[action]) : PhySouth;
}

int controller_binding(const Settings& s, size_t action) {
    if (action >= kControlCount) return PhySouth;
    return s.controller_custom_mask & (1u << action) ? std::clamp(s.controller_bindings[action], 0, int(kControlCount) - 1)
                                                     : default_controller_binding(action, s.controller_preset);
}

bool load_settings(const std::string& path, Settings& s) {
    std::ifstream file(path);
    if (!file) return false;
    std::string line;
    int game_language_version = 0;
    while (std::getline(file, line)) {
        int v = 0;
        unsigned u = 0;
        const char* l = line.c_str();
        if (std::sscanf(l, "width=%d", &v) == 1 && v >= 640 && v <= 7680) s.width = v;
        else if (std::sscanf(l, "height=%d", &v) == 1 && v >= 360 && v <= 4320) s.height = v;
        else if (std::sscanf(l, "internal_resolution=%d", &v) == 1) s.internal_resolution = std::clamp(v, 0, 6);
        else if (std::sscanf(l, "volume=%d", &v) == 1) s.volume = std::clamp(v, 0, 200);
        else if (std::sscanf(l, "muted=%d", &v) == 1) s.muted = v != 0;
        else if (std::sscanf(l, "fullscreen=%d", &v) == 1) s.fullscreen = v != 0;
        else if (std::sscanf(l, "always_on_top=%d", &v) == 1) s.always_on_top = v != 0;
        else if (std::sscanf(l, "language=%d", &v) == 1) s.language = std::clamp(v, 0, 12);
        else if (std::sscanf(l, "game_language=%d", &v) == 1) s.game_language = std::clamp(v, 0, 11);
        else if (std::sscanf(l, "game_language_version=%d", &v) == 1) game_language_version = v;
        else if (std::sscanf(l, "memory_limit_gb=%d", &v) == 1) s.memory_limit_gb = std::clamp(v, 2, 32);
        else if (std::sscanf(l, "low_vram_profile=%d", &v) == 1) s.low_vram_profile = v != 0;
        else if (std::sscanf(l, "keyboard_separate_player=%d", &v) == 1) s.keyboard_separate_player = v != 0;
        else if (std::sscanf(l, "button_icons=%d", &v) == 1) s.button_icons = std::clamp(v, 0, 4);
        else if (std::sscanf(l, "controller_preset=%d", &v) == 1) s.controller_preset = std::clamp(v, 0, 2);
        else if (std::sscanf(l, "metrics_enabled=%d", &v) == 1) s.metrics_enabled = v != 0;
        else if (std::sscanf(l, "metric_fps=%d", &v) == 1) s.metric_fps = v != 0;
        else if (std::sscanf(l, "metric_one_percent_low=%d", &v) == 1) s.metric_one_percent_low = v != 0;
        else if (std::sscanf(l, "metric_game_speed=%d", &v) == 1) s.metric_game_speed = v != 0;
        else if (std::sscanf(l, "metric_frametime_graph=%d", &v) == 1) s.metric_frametime_graph = v != 0;
        else if (std::sscanf(l, "overlay_position=%d", &v) == 1) s.overlay_position = std::clamp(v, 0, 3);
        else if (std::sscanf(l, "upscaler=%d", &v) == 1) s.upscaler = std::clamp(v, 0, 1);
        else if (std::sscanf(l, "preserve_aspect=%d", &v) == 1) s.preserve_aspect = v != 0;
        else if (std::sscanf(l, "texture_cache=%d", &v) == 1) s.texture_cache = v != 0;
        else if (std::sscanf(l, "fsr_sharpness=%d", &v) == 1) s.fsr_sharpness = std::clamp(v, 0, 200);
        else if (std::sscanf(l, "antialiasing=%d", &v) == 1) s.antialiasing = std::clamp(v, 0, 1);
        else if (std::sscanf(l, "anisotropy=%d", &v) == 1) s.anisotropy = std::clamp(v, 0, 16);
        else if (std::sscanf(l, "rumble=%d", &v) == 1) s.rumble = std::clamp(v, 0, 100);
        else if (std::sscanf(l, "vsync=%d", &v) == 1) s.vsync = std::clamp(v, 0, 3);
        else if (std::sscanf(l, "shader_bundle=%d", &v) == 1) s.shader_bundle = v != 0;
        else if (std::sscanf(l, "framegen=%d", &v) == 1) s.framegen = v != 0;
        else if (std::sscanf(l, "render_ahead=%d", &v) == 1) s.render_ahead = std::clamp(v, 1, 2);
        else if (std::sscanf(l, "framegen_multiplier=%d", &v) == 1) s.framegen_multiplier = std::clamp(v, 2, 8);
        else if (std::sscanf(l, "pipeline_skip_draws=%d", &v) == 1) s.pipeline_skip_draws = v != 0;
        else if (std::sscanf(l, "frame_cap=%d", &v) == 1)
            s.frame_cap = v == 30 || v == 60 || v == 90 || v == 120 || v == 144 ? v : 60;
        else if (std::sscanf(l, "controller_custom_mask=%u", &u) == 1) s.controller_custom_mask = u;
        else {
            int index = -1, binding = -1;
            if (std::sscanf(l, "keyboard_%d=%d", &index, &binding) == 2 && index >= 0 && index < int(kControlCount) &&
                binding >= 0 && binding <= 511)
                s.keyboard_bindings[size_t(index)] = index == int(CtlMinus) && binding ==
#if defined(_WIN32)
                    27 ? 8 : binding;  // Esc is reserved for settings; game Minus moves to Backspace.
#else
                    41 ? 42 : binding;  // SDL scancodes
#endif
            else if (std::sscanf(l, "controller_%d=%d", &index, &binding) == 2 && index >= 0 &&
                     index < int(kControlCount) && binding >= 0 && binding < int(kControlCount))
                s.controller_bindings[size_t(index)] = binding;
        }
    }
    // Before version 2, IDs 8 and 10 were Arabic and Turkish. They are no
    // longer selectable game languages; do not reinterpret saved values as Dutch/Chinese.
    if (game_language_version < 2 && (s.game_language == 8 || s.game_language == 10)) s.game_language = 0;
    return true;
}

void save_settings(const std::string& path, const Settings& s) {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    f << "width=" << s.width << "\nheight=" << s.height << "\nvolume=" << s.volume
      << "\ninternal_resolution=" << s.internal_resolution << "\nmuted=" << s.muted << "\nfullscreen=" << s.fullscreen
      << "\nalways_on_top=" << s.always_on_top << "\nlanguage=" << s.language << "\ngame_language=" << s.game_language
      << "\ngame_language_version=2"
      << "\nmemory_limit_gb=" << s.memory_limit_gb << "\nlow_vram_profile=" << s.low_vram_profile
      << "\nkeyboard_separate_player=" << s.keyboard_separate_player << "\nbutton_icons=" << s.button_icons << "\ncontroller_preset=" << s.controller_preset << "\nmetrics_enabled=" << s.metrics_enabled
      << "\nmetric_fps=" << s.metric_fps << "\nmetric_one_percent_low=" << s.metric_one_percent_low
      << "\nmetric_game_speed=" << s.metric_game_speed << "\nmetric_frametime_graph=" << s.metric_frametime_graph
      << "\noverlay_position=" << s.overlay_position << "\ncontroller_custom_mask=" << s.controller_custom_mask
      << "\nupscaler=" << s.upscaler << "\nfsr_sharpness=" << s.fsr_sharpness << "\nantialiasing=" << s.antialiasing
      << "\nanisotropy=" << s.anisotropy << "\nrumble=" << s.rumble << "\nvsync=" << s.vsync
      << "\nframe_cap=" << s.frame_cap << "\nshader_bundle=" << s.shader_bundle << "\nframegen=" << s.framegen << "\nframegen_multiplier=" << s.framegen_multiplier << "\nrender_ahead=" << s.render_ahead << "\npreserve_aspect=" << s.preserve_aspect << "\ntexture_cache=" << s.texture_cache << "\npipeline_skip_draws=" << s.pipeline_skip_draws << '\n';
    for (size_t i = 0; i < kControlCount; ++i)
        f << "keyboard_" << i << '=' << s.keyboard_bindings[i] << '\n'
          << "controller_" << i << '=' << s.controller_bindings[i] << '\n';
}

}  // namespace nx::overlay
