#include "settings.h"
#include "nvn/framegen_timing.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main() {
    using namespace nx::overlay;
    auto path = (std::filesystem::temp_directory_path() / "superwonders-settings-test.ini").string();
    check(nx::nvn::framegen_present_period(60,1) == 16'666'666, "60 Hz interpolation cadence");
    check(nx::nvn::framegen_present_period(30,1) == 33'333'333, "30 FPS cap keeps a 30 Hz source cadence");
    check(nx::nvn::framegen_present_period(60,2) == 33'333'333, "game-requested half-rate cadence");
    check(nx::nvn::framegen_present_period(120,1) == 8'333'333, "explicit high-frame-rate cadence");
    Settings s;
    check(!s.pipeline_skip_draws, "preserve draws by default");
    // Existing settings files must pick up the new default while retaining user options.
    { std::ofstream f(path); f << "frame_cap=144\nmetrics_enabled=1\n"; }
    check(load_settings(path, s), "load old settings");
    check(!s.pipeline_skip_draws && s.frame_cap == 144 && s.metrics_enabled, "old settings migration");
    s.pipeline_skip_draws = true;
    s.shader_bundle = false;
    s.framegen = true;
    s.button_icons = 4; s.keyboard_separate_player = true;
    save_settings(path, s);
    Settings t;
    check(load_settings(path, t) && t.pipeline_skip_draws && !t.shader_bundle && t.framegen && t.button_icons == 4 && t.keyboard_separate_player, "opt-in survives restart");
    t.pipeline_skip_draws = false;
    save_settings(path, t);
    check(load_settings(path, s) && !s.pipeline_skip_draws, "opt-out survives restart");
    s.width = 1920; s.height = 1080; s.volume = 73; s.game_language = 2;
    s.keyboard_bindings[CtlA] = 99; s.controller_custom_mask = 1;
    for (auto profile : {GraphicsProfile::Performance, GraphicsProfile::Balanced, GraphicsProfile::Quality}) {
        auto configured = s;
        apply_graphics_profile(configured, profile);
        check(configured.width == s.width && configured.height == s.height && configured.volume == s.volume &&
              configured.game_language == s.game_language && configured.keyboard_bindings == s.keyboard_bindings &&
              configured.controller_custom_mask == s.controller_custom_mask && configured.shader_bundle == s.shader_bundle, "graphics presets preserve personal settings");
        check(!configured.pipeline_skip_draws && !configured.framegen && configured.frame_cap == 60, "presets retain complete-image rendering and a normal speed cap");
    }
    std::remove(path.c_str());
    std::puts("settings tests passed");
}
