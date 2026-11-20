// Between the overlay's parts (overlay.cpp: state, input, metrics, frame; overlay_menu.cpp: the menu's tabs;
// overlay_vk.cpp: drawing). Everything here runs on the presenting thread.
#pragma once

#include <cstdint>
#include <mutex>

#include "overlay/overlay.h"

struct ImDrawData;

namespace nx::overlay {

// Whether this frame has anything to show (the menu is open, the metrics panel is on, or input is pending): when not,
// ImGui is not run at all.
bool frame_wanted();
void ensure_context();  // the ImGui context, style and fonts
// The frame's ImGui content for an image of `w` x `h` pixels; null when there is nothing to draw.
ImDrawData* build_frame(uint32_t w, uint32_t h);

// Tab and Controls subsection are published/consumed together under a short
// mutex. Only the render thread owns the visible section. Generation tags keep
// paused old input producers/close callbacks from changing a reopened menu.
struct MenuRoute { int tab = -1, controls_section = -1; };
class MenuRouteRequests {
    std::mutex mutex_;
    MenuRoute pending_;
    uint64_t high_water_ = 0;
    // Caller holds mutex_. A newer menu invalidates the preceding menu's route
    // even when that new menu has no navigation request of its own.
    bool accept_locked(uint64_t generation) noexcept {
        if (generation < high_water_) return false;
        if (generation > high_water_) {
            high_water_ = generation;
            pending_ = {};
        }
        return true;
    }
public:
    // Defaults preserve standalone generation-zero callers. Runtime callers
    // must tag every operation with the generation observed for that action.
    void tab(int value, uint64_t generation = 0) noexcept {
        std::lock_guard lock(mutex_);
        if (accept_locked(generation)) pending_ = {value, value == 2 ? 0 : -1};
    }
    void controls(int section, uint64_t generation = 0) noexcept {
        std::lock_guard lock(mutex_);
        if (accept_locked(generation)) pending_ = {2, section};
    }
    void clear(uint64_t generation = 0) noexcept {
        std::lock_guard lock(mutex_);
        if (accept_locked(generation)) pending_ = {};
    }
    MenuRoute take(uint64_t generation = 0) noexcept {
        std::lock_guard lock(mutex_);
        if (!accept_locked(generation)) return {};
        const MenuRoute result = pending_;
        pending_ = {};
        return result;
    }
};

// overlay_menu.cpp
struct MenuContext {
    Settings& settings;
    const Host& host;
    int& capture_action;       // keyboard binding being captured (-1: none)
    int tab_request;           // -1, or the tab to select (shoulder buttons)
    bool& open;
    float scale;     // the image height / 720
    bool appearing;  // first frame after opening
    float height;    // the image height in pixels (the style is sized from it)
    float reveal;    // 0..1: the open / close fade
    const Settings* startup_settings = nullptr;  // null while configuring in the launcher
    int controls_section_request = -1;
};
void draw_menu(MenuContext& c);
int menu_tab_count();
const char* const* menu_language_names();  // 10 entries

// overlay.cpp: apply and persist after a change made in the menu
void settings_changed(bool persist);

// overlay_progress.cpp: the startup shader-compile screen (shader_progress in overlay.h feeds it)
bool progress_wanted();
void draw_progress(float height);

}  // namespace nx::overlay
