// The overlay's state, input queue, metrics and per-frame ImGui work (see overlay.h). The menu's tabs are in
// overlay_menu.cpp, the drawing in overlay_vk.cpp.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "imgui.h"

#include <nx/runtime.h>

#include "overlay/overlay_internal.h"
#include "overlay/usability.h"
#include "overlay/controller_remapping.h"
#include "overlay/overlay_ui.h"
#include "netplay_game.h"

namespace nx::overlay {

namespace {

struct Event {
    enum Kind { KeyEv, Move, Button, Wheel, Text } kind;
    int raw = 0;
    Key key = KeyNone;
    bool down = false;
    float x = 0, y = 0;
    uint32_t ch = 0;
    uint64_t generation = 0;
};

std::mutex g_lock;  // g_events, g_pending, g_host
std::vector<Event> g_events;
Host g_host;
bool g_has_pending = false;
Settings g_pending;   // from the platform (sync), taken by the next frame
Settings g_settings;  // the presenting thread's copy
Settings g_startup_settings;
bool g_game_started = false;  // protected by g_lock
PadNavigationGate g_pad_gate;
PadNavigationGate::Frame g_navigation_frame; // presenting thread only
std::atomic<ScreenFn> g_screen{nullptr};  // a full-window screen under the menu (the launcher), or none
// Input goes to the overlay: the menu is open, or a screen is shown.
bool taking_input() { return g_pad_gate.is_open() || g_screen.load(); }
std::atomic<bool> g_metrics{false};
std::atomic<int> g_input_frames{0};  // frames to run after input while closed (hover/close animations)
std::atomic<bool> g_fading{false};   // the menu is still fading out

MenuRouteRequests g_tab_request;
std::atomic<double> g_render_fps{0};
std::atomic<bool> g_interpolated{false};

int g_capture = -1;  // keyboard binding being captured
int g_tab = 0;

// Present intervals for the metrics panel (as platform_win.cpp's overlay).
constexpr size_t kHistory = 300;
std::mutex g_metrics_lock;
std::array<double, kHistory> g_intervals{};
size_t g_next = 0, g_count = 0;
uint64_t g_previous_ns = 0;
std::atomic<int> g_game_interval{1};
std::atomic<double> g_game_speed{100.0};

ImGuiKey imgui_key(Key k) {
    switch (k) {
        case KeyTab: return ImGuiKey_Tab;
        case KeyLeft: return ImGuiKey_LeftArrow;
        case KeyRight: return ImGuiKey_RightArrow;
        case KeyUp: return ImGuiKey_UpArrow;
        case KeyDown: return ImGuiKey_DownArrow;
        case KeyPageUp: return ImGuiKey_PageUp;
        case KeyPageDown: return ImGuiKey_PageDown;
        case KeyHome: return ImGuiKey_Home;
        case KeyEnd: return ImGuiKey_End;
        case KeyEnter: return ImGuiKey_Enter;
        case KeyEscape: return ImGuiKey_Escape;
        case KeyBackspace: return ImGuiKey_Backspace;
        case KeyDelete: return ImGuiKey_Delete;
        case KeySpace: return ImGuiKey_Space;
        case KeyShift: return ImGuiKey_LeftShift;
        case KeyCtrl: return ImGuiKey_LeftCtrl;
        default: return ImGuiKey_None;
    }
}

void push(Event e, uint64_t generation) {
    e.generation = generation;
    std::lock_guard lk(g_lock);
    if (g_events.size() < 4096) g_events.push_back(e);
}

// The metrics panel: a small card in the chosen corner, in the menu's colours. Savage Wind's performance overlay
// (menu.mm perf_overlay_draw) is the model: names in colour, values beside them, the frame times as a graph.
void draw_metrics(float height) {
    std::array<float, kHistory> samples{};
    size_t count = 0;
    {
        std::lock_guard lk(g_metrics_lock);
        count = g_count;
        size_t start = (g_next + kHistory - count) % kHistory;
        for (size_t i = 0; i < count; ++i) samples[i] = float(g_intervals[(start + i) % kHistory]);
    }
    double sum = 0;
    size_t recent = std::min<size_t>(count, 60);
    for (size_t i = count - recent; i < count; ++i) sum += samples[i];
    double fps = sum > 0 ? double(recent) * 1000.0 / sum : 0.0;
    double low = 0;
    if (count) {
        std::vector<float> slow(samples.begin(), samples.begin() + long(count));
        std::sort(slow.begin(), slow.end(), std::greater<float>());
        size_t n = std::max<size_t>(1, (count + 99) / 100);
        double s = 0;
        for (size_t i = 0; i < n; ++i) s += slow[i];
        if (s > 0) low = double(n) * 1000.0 / s;
    }
    const int interval = std::max(1, g_game_interval.load());
    const double speed = g_game_speed.load(std::memory_order_relaxed);
    const Settings& st = g_settings;
    const bool interpolated = g_interpolated.load();
    const double game_target = st.frame_cap > 60 ? double(st.frame_cap) : std::min(double(st.frame_cap),60.0/double(interval));
    const double target = game_target * (interpolated ? std::clamp(st.framegen_multiplier, 2, 8) : 1);
    const float k = std::clamp(height / 1080.0f, 0.5f, 3.0f);
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    const float m = 16 * k;
    const int pos = std::clamp(st.overlay_position, 0, 3);
    const ImVec2 at{pos & 1 ? size.x - m : m, pos & 2 ? size.y - m : m};
    ImGui::SetNextWindowPos(at, ImGuiCond_Always, {pos & 1 ? 1.0f : 0.0f, pos & 2 ? 1.0f : 0.0f});
    const ui::Theme& th = ui::theme();
    ImVec4 bg = ImGui::ColorConvertU32ToFloat4(th.panel);
    bg.w = 0.82f;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * k, 12 * k));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12 * k);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12 * k, 3 * k));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6 * k, 1 * k));
    const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                               ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                               ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##metrics", nullptr, f)) {
        constexpr float kScale = 0.74f;
        ImGui::SetWindowFontScale(kScale);
        // green at the target frame rate, amber a little under it, red well under (MangoHud colours its numbers so)
        const auto rate = [&](double v, double expected) {
            return ImGui::ColorConvertU32ToFloat4(v >= expected * 0.95 ? th.good : v >= expected * 0.75 ? th.warn : th.bad);
        };
        const ImVec4 name_c = ImGui::ColorConvertU32ToFloat4(th.accent2), plain = ImGui::ColorConvertU32ToFloat4(th.text);
        const auto line = [&](const char* name, const ImVec4& colour, const char* fmt, double v, const char* unit) {
            ImGui::TableNextColumn();
            ImGui::TextColored(name_c, "%s", name);
            ImGui::TableNextColumn();
            ImGui::PushFont(ui::title());  // (the numbers in the heavier face, at the body size)
            ImGui::SetWindowFontScale(kScale * 40.0f / 72.0f);
            ImGui::TextColored(colour, fmt, v);
            ImGui::PopFont();
            ImGui::SetWindowFontScale(kScale);
            if (*unit) {
                ImGui::SameLine(0, 5 * k);
                ImGui::TextDisabled("%s", unit);
            }
        };
        if (ImGui::BeginTable("##m", 2, ImGuiTableFlags_SizingFixedFit)) {
            if (st.metric_fps) line(interpolated ? "Presented FPS" : "FPS", rate(fps, target), "%.0f", fps, "");
            if (st.metric_fps && interpolated) {
                const double game_fps = g_render_fps.load();
                line("Game FPS", rate(game_fps, game_target), "%.0f", game_fps, "");
            }
            if (st.metric_fps && fps > 0) line("Frame", plain, "%.1f", 1000.0 / fps, "ms");
            if (st.metric_one_percent_low) line("1% low", rate(low, target), "%.0f", low, "FPS");
            if (st.metric_game_speed) line("Speed", speed >= 97 ? plain : ImGui::ColorConvertU32ToFloat4(th.warn), "%.0f", speed, "%");
            if (lan::active()) {
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s",st.language==2?"Positionscheck":"Position check");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s",lan::player_probe_ready()?(st.language==2?"teilweise":"partial"):(st.language==2?"ausstehend":"pending"));
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("One player only; pending means no agreeing baseline. Other actors and world state are not verified.");
                const auto wait = lan::timing();
                const double budget=1000.0/double(lan::simulation_rate());
                if (wait.ticks) {
                    const bool german = st.language == 2;
                    line(german ? "LAN Wartezeit" : "LAN input wait",
                         ImGui::ColorConvertU32ToFloat4(wait.smoothed_ms > 50 ? th.bad : wait.smoothed_ms > budget ? th.warn : th.text),
                         "%.1f", wait.smoothed_ms, "ms");
                }
                const bool detailed=st.metric_frametime_graph || st.metric_game_speed || st.metric_one_percent_low;
                uint32_t slowest=0;
                double worst=0;
                for(uint32_t player=0;player<4;++player) {
                    const auto peer=lan::peer_timing(player);
                    if(peer.samples && peer.smoothed_ms>worst) { worst=peer.smoothed_ms; slowest=player; }
                }
                for(uint32_t player=0;player<4;++player) {
                    if(!detailed && player!=slowest) continue;
                    const auto peer=lan::peer_timing(player);
                    if(!peer.samples) continue;
                    char label[32]; std::snprintf(label,sizeof label,st.language==2?"PC %u ohne LAN":"PC %u outside LAN",player+1);
                    line(label,ImGui::ColorConvertU32ToFloat4(peer.smoothed_ms>budget*1.5?th.bad:peer.smoothed_ms>budget*1.1?th.warn:th.text),
                         "%.1f",peer.smoothed_ms,"ms");
                }
            }
            ImGui::EndTable();
        }
        if (st.metric_frametime_graph && count > 1) {  // the frame times, 0-50 ms, with lines at 16.7 and 33.3 ms
            ImGui::Dummy(ImVec2(0, 2 * k));
            const ImVec2 p = ImGui::GetCursorScreenPos(), gs(300 * k, 64 * k);
            ImGui::Dummy(gs);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + gs.x, p.y + gs.y), IM_COL32(255, 255, 255, 12), 6 * k);
            const auto y_of = [&](float ms) { return p.y + gs.y - std::clamp(ms / 50.0f, 0.0f, 1.0f) * gs.y; };
            for (float ms : {16.7f, 33.3f})
                dl->AddLine(ImVec2(p.x, y_of(ms)), ImVec2(p.x + gs.x, y_of(ms)), IM_COL32(255, 255, 255, 36), 1.0f);
            const float dx = gs.x / float(kHistory - 1);
            const float x0 = p.x + gs.x - dx * float(count - 1);
            std::array<ImVec2, kHistory> pts;
            const float late = 17.5f * float(interval);
            for (size_t i = 0; i < count; ++i) {
                const float x = x0 + dx * float(i), y = y_of(samples[i]);
                pts[i] = ImVec2(x, y);
                const ImU32 c = samples[i] > 33.4f ? (th.bad & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 120)
                                : samples[i] > late ? (th.warn & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 90)
                                                    : (th.accent & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 34);
                dl->AddRectFilled(ImVec2(x, y), ImVec2(x + dx + 0.5f, p.y + gs.y), c);
            }
            dl->AddPolyline(pts.data(), int(count), (th.accent & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 230), 0, std::max(1.0f, 1.5f * k));
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();
}

// Controller navigation: the pad (Nintendo button meanings) as ImGui's gamepad keys. A confirms, B cancels.
void feed_pad(ImGuiIO& io) {
    // Physical button polling still reaches the remapper, but a binding press
    // must not also activate a focused menu control or cancel a popup.
    const bool open = taking_input() && g_navigation_frame.navigation_allowed &&
        g_navigation_frame.state.generation == g_pad_gate.generation() &&
        g_capture < 0 && controller_remap_state().capture < 0;
    uint64_t b = open ? g_navigation_frame.nav.buttons : 0;
    int32_t lx = open ? g_navigation_frame.nav.lx : 0, ly = open ? g_navigation_frame.nav.ly : 0;
    struct Map {
        uint64_t bit;
        ImGuiKey key;
    };
    static const Map map[] = {
        {plat::kA, ImGuiKey_GamepadFaceDown},     {plat::kB, ImGuiKey_GamepadFaceRight},
        {plat::kX, ImGuiKey_GamepadFaceUp},       {plat::kY, ImGuiKey_GamepadFaceLeft},
        {plat::kLeft, ImGuiKey_GamepadDpadLeft},  {plat::kRight, ImGuiKey_GamepadDpadRight},
        {plat::kUp, ImGuiKey_GamepadDpadUp},      {plat::kDown, ImGuiKey_GamepadDpadDown},
        {plat::kZL, ImGuiKey_GamepadL2},          {plat::kZR, ImGuiKey_GamepadR2},
    };
    for (const Map& m : map) io.AddKeyEvent(m.key, (b & m.bit) != 0);
    auto axis = [&](ImGuiKey neg, ImGuiKey posk, int32_t v) {
        float f = float(v) / float(plat::kStickMax);
        io.AddKeyAnalogEvent(neg, f < -0.3f, f < 0 ? -f : 0.0f);
        io.AddKeyAnalogEvent(posk, f > 0.3f, f > 0 ? f : 0.0f);
    };
    axis(ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight, lx);
    axis(ImGuiKey_GamepadLStickDown, ImGuiKey_GamepadLStickUp, ly);  // Horizon: up is positive
}

}  // namespace

bool enabled() {
    static const bool on = [] {
        const char* e = std::getenv("NX_IMGUI_MENU");
        return !(e && e[0] == '0');
    }();
    return on;
}

int menu_language() { return std::clamp(g_settings.language, 0, 12); }

void init(const Host& host, const Settings& s) {
    std::lock_guard lk(g_lock);
    g_host = host;
    g_pending = s, g_has_pending = true;
    g_metrics = s.metrics_enabled;
}

void sync(const Settings& s) {
    std::lock_guard lk(g_lock);
    g_pending = s, g_has_pending = true;
    g_metrics = s.metrics_enabled;
}

// Presenting thread only. Compare effective bindings, so visual-only model or
// unrelated settings changes do not repeatedly disarm navigation.
void quarantine_changed_controller_bindings() {
    static bool initialized=false;
    static std::array<int,16> previous{};
    std::array<int,16> current{};
    for(size_t action=0;action<current.size();++action)current[action]=controller_binding(g_settings,action);
    if(initialized && current!=previous)g_pad_gate.quarantine_navigation();
    previous=current;initialized=true;
}

void settings_changed(bool persist) {
    g_metrics = g_settings.metrics_enabled;
    Host h;
    {
        std::lock_guard lk(g_lock);
        h = g_host;
    }
    if (h.apply) h.apply(g_settings, persist);
}

Settings current_settings() {
    std::lock_guard lk(g_lock);
    return g_has_pending ? g_pending : g_settings;
}

std::string key_binding_name(int key) {
    std::lock_guard lock(g_lock);
    return g_host.key_name ? g_host.key_name(key) : "";
}

void apply_settings(const Settings& s, bool persist) {
    sync(s);
    g_settings = s;
    settings_changed(persist);
}

void mark_game_started() {
    std::lock_guard lock(g_lock);
    g_startup_settings = g_has_pending ? g_pending : g_settings;
    g_game_started = true;
}

int aspect_ratio() { return std::clamp(current_settings().aspect_ratio, 0, 2); }

void set_aspect_ratio(int ratio) {
    auto settings = current_settings();
    settings.aspect_ratio = std::clamp(ratio, 0, 2);
    apply_settings(settings);
}

void key_event(int raw, Key key, bool down) {
    const auto generation = g_pad_gate.generation();
    if (key == KeyF1 && down) {
        toggle();
        return;
    }
    if (key == KeyEscape && down && !taking_input()) {
        toggle();
        return;
    }
    if (!taking_input() && key != KeyNone) return;
    push({Event::KeyEv, raw, key, down}, generation);
}
void mouse_move(float x, float y) {
    const auto generation = g_pad_gate.generation();
    if (taking_input()) push({Event::Move, 0, KeyNone, false, x, y}, generation);
}
void mouse_button(int button, bool down) {
    const auto generation = g_pad_gate.generation();
    if (taking_input()) push({Event::Button, button, KeyNone, down}, generation);
}
void mouse_wheel(float dy) {
    const auto generation = g_pad_gate.generation();
    if (taking_input()) push({Event::Wheel, 0, KeyNone, false, 0, dy}, generation);
}
void text_input(uint32_t ch) {
    const auto generation = g_pad_gate.generation();
    if (taking_input()) push({Event::Text, 0, KeyNone, false, 0, 0, ch}, generation);
}
void menu_transition(const PadNavigationGate::Transition& transition, const char* why) {
    if (!transition.state.open) g_tab_request.clear(transition.state.generation);
    if (!transition.changed) return;
    g_input_frames = 30;
    log("overlay: menu %s (%s)", transition.state.open ? "opened" : "closed", why);
}
void set_open(bool open, const char* why) { menu_transition(g_pad_gate.set_open(open), why); }

void toggle() { menu_transition(g_pad_gate.toggle(), "toggle"); }
void open_settings(int tab) {
    const auto transition = g_pad_gate.set_open(true);
    g_tab_request.tab(std::clamp(tab, 0, menu_tab_count() - 1), transition.state.generation);
    menu_transition(transition, "launcher shortcut");
}
void open_controls(int section) {
    const auto transition = g_pad_gate.set_open(true);
    g_tab_request.controls(std::clamp(section, 0, 2), transition.state.generation);
    menu_transition(transition, "launcher controls shortcut");
}
bool is_open() { return g_pad_gate.is_open(); }
bool wants_mouse() { return taking_input(); }
bool wants_keyboard() { return taking_input(); }
void set_screen(ScreenFn fn) {
    g_screen = fn;
    g_input_frames = 30;
}
bool screen_shown() { return g_screen.load() != nullptr; }
uint64_t pad_buttons() { return g_pad_gate.raw().buttons; }
uint64_t navigation_buttons() {
    return taking_input() && g_navigation_frame.navigation_allowed &&
        g_navigation_frame.state.generation == g_pad_gate.generation() &&
        g_capture < 0 && controller_remap_state().capture < 0 ? g_navigation_frame.nav.buttons : 0;
}

PadNavigationGate::Ticket begin_pad_poll() {
    return enabled() ? g_pad_gate.begin_sample() : PadNavigationGate::Ticket{};
}
void filter_pad(plat::PadState& p) { filter_pad(p, begin_pad_poll()); }
void filter_pad(plat::PadState& p, PadNavigationGate::Ticket ticket, uint64_t physical_buttons) {
    if (!enabled()) return;
    const uint64_t combo = plat::kPlus | plat::kMinus;
    const bool combo_now = (p.buttons & combo) == combo;
    const auto publication = g_pad_gate.publish(ticket, {p.buttons, p.lx, p.ly, physical_buttons}, combo);
    if (publication.transition.changed) menu_transition(publication.transition, "controller chord");
    if (taking_input() || combo_now) p = plat::PadState{};
}

void rendered(uint64_t now_ns, bool interpolated) {
    static uint64_t previous = 0;
    static std::array<uint64_t, 60> intervals{};
    static size_t next = 0, count = 0;
    if (previous && now_ns > previous) {
        intervals[next++ % intervals.size()] = now_ns-previous;
        count = std::min(count+1,intervals.size());
        uint64_t total = 0;
        for (size_t i = 0; i < count; ++i) total += intervals[i];
        if (total) g_render_fps = double(count)*1e9/double(total);
    }
    previous = now_ns; g_interpolated = interpolated;
}

void presented(uint64_t now_ns) {
    std::lock_guard lk(g_metrics_lock);
    if (g_previous_ns && now_ns > g_previous_ns) {
        double ms = double(now_ns - g_previous_ns) / 1e6;
        if (ms > 0 && ms < 10000) {
            g_intervals[g_next] = ms;
            g_next = (g_next + 1) % kHistory;
            g_count = std::min(kHistory, g_count + 1);
        }
    }
    g_previous_ns = now_ns;
}

void game_interval(int interval) { g_game_interval = std::max(1, interval); }
void game_speed(double percent) { g_game_speed.store(std::clamp(percent, 0.0, 1000.0), std::memory_order_relaxed); }

bool frame_wanted() {
    return g_pad_gate.is_open() || g_screen.load() || g_metrics || g_input_frames > 0 || g_fading || progress_wanted() || ui::notices_active();
}

void notify(const char* text) { ui::notify(text); }

void ensure_context() {
    if (ImGui::GetCurrentContext()) return;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // no imgui.ini next to the game
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.BackendPlatformName = "nx_overlay";
    io.ConfigNavMoveSetMousePos = false;
    ui::load_fonts();
}

ImDrawData* build_frame(uint32_t w, uint32_t h) {
    // Unattended shutdown regression: use the same render-thread exit path as
    // the menu button, without depending on the current navigation focus.
    static const uint64_t quit_frame=[] {
        const auto* value=std::getenv("NX_TEST_QUIT_FRAME");
        return value?std::strtoull(value,nullptr,10):uint64_t(0);
    }();
    if (quit_frame && g_frame.load()>=quit_frame) {
        log("overlay: scripted render-thread shutdown at frame %llu",(unsigned long long)g_frame.load());
        plat::exit_process(0);
    }
    // An actively reviewed run can request the same clean shutdown after its scenario completes.
    static const char* quit_file=std::getenv("NX_TEST_QUIT_FILE");
    static uint64_t next_quit_check=0;
    if (quit_file && *quit_file) {
        const auto now=plat::monotonic_ns();
        if (now>=next_quit_check) {
            next_quit_check=now+250'000'000;
            if (FILE* request=plat::fopen_utf8(quit_file,"rb")) {
                std::fclose(request);
                log("overlay: scripted render-thread shutdown by request at frame %llu",(unsigned long long)g_frame.load());
                plat::exit_process(0);
            }
        }
    }
    ImGuiIO& io = ImGui::GetIO();
    static uint64_t last_ns = 0;
    uint64_t now = plat::monotonic_ns();
    io.DeltaTime = last_ns && now > last_ns ? std::min(float(double(now - last_ns) / 1e9), 0.25f) : 1.0f / 60.0f;
    last_ns = now;
    io.DisplaySize = {float(w), float(h)};
    const float scale = std::clamp(float(h) / 720.0f, 0.75f, 4.0f);
    io.FontGlobalScale = ui::font_scale(float(h));
    ui::apply_style(float(h));

    std::vector<Event> events;
    Settings startup_settings;
    bool game_started = false;
    {
        std::lock_guard lk(g_lock);
        events.swap(g_events);
        if (g_has_pending) g_settings = g_pending, g_has_pending = false;
        startup_settings = g_startup_settings;
        game_started = g_game_started;
    }
    static uint64_t seen_menu_epoch = 0;
    const auto menu_epoch = g_pad_gate.generation();
    if (menu_epoch != seen_menu_epoch) {
        seen_menu_epoch = menu_epoch;
        // Old queued releases are discarded too: clear ImGui's held state so
        // stale key/button ups cannot clobber fresh presses in the new menu.
        const auto pointer=io.MousePos;
        io.ClearEventsQueue();io.ClearInputKeys();io.ClearInputMouse();
        io.AddMousePosEvent(pointer.x,pointer.y); // A stationary pointer remains usable.
        g_capture = -1;
        cancel_controller_remapping(); // Includes close/reopen between two frames.
    }

    if (!g_pad_gate.is_open()) { g_capture = -1; cancel_controller_remapping(); }
    g_pad_gate.set_capture(g_capture >= 0 || controller_remap_state().capture >= 0);
    bool changed = false;
    for (const Event& e : events) {
        if(e.generation!=g_pad_gate.generation())continue;
        switch (e.kind) {
            case Event::KeyEv:
                if (g_capture >= 0 && e.down) {  // the next key becomes the binding; Esc cancels
                    if (e.key != KeyEscape) {
                        const int binding=e.key==KeyDelete?0:e.raw;
                        if(g_capture<int(kControlCount))g_settings.keyboard_bindings[size_t(g_capture)]=binding;
                        else if(g_capture==int(kControlCount))g_settings.keyboard_standee_key=binding;
                        changed=true;
                    }
                    g_capture = -1;
                    break;
                }
                if (g_pad_gate.is_open() && e.down && e.key == KeyEscape && controller_remap_state().capture >= 0) {
                    cancel_controller_remapping(); // Cancel listening; leave settings open.
                    break;
                }
                if (controller_remap_state().capture >= 0 && e.down) break;
                if (g_pad_gate.is_open() && controller_remap_state().capture < 0 && e.down && (e.key == KeyPageUp || e.key == KeyPageDown)) {
                    g_tab_request.tab((g_tab + (e.key == KeyPageDown ? 1 : menu_tab_count()-1)) % menu_tab_count(), menu_epoch);
                    break;
                }
                if (e.key == KeyEscape && e.down && g_pad_gate.is_open() && g_capture < 0 &&
                    !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)) {
                    set_open(false, "Esc");
                    break;
                }
                if (ImGuiKey k = imgui_key(e.key); k != ImGuiKey_None) io.AddKeyEvent(k, e.down);
                break;
            case Event::Move: io.AddMousePosEvent(e.x, e.y); break;
            case Event::Button: io.AddMouseButtonEvent(e.raw, e.down); break;
            case Event::Wheel: io.AddMouseWheelEvent(0, e.y); break;
            case Event::Text: io.AddInputCharacter(e.ch); break;
        }
    }
    if (changed) settings_changed(true);
    // B on a controller backs out: inside a dropdown or an edit ImGui handles it; at the top it closes the menu.
    const bool capturing = g_capture >= 0 || controller_remap_state().capture >= 0;
    g_pad_gate.set_capture(capturing);
    quarantine_changed_controller_bindings();
    g_navigation_frame = g_pad_gate.consume();
    const uint64_t pressed = g_navigation_frame.pressed;

    const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup);
    if (g_pad_gate.is_open() && g_navigation_frame.state.open &&
        g_navigation_frame.state.generation == g_pad_gate.generation() &&
        g_navigation_frame.navigation_allowed && !popup) {
        if ((pressed & plat::kB) && !ImGui::IsAnyItemActive()) set_open(false, "B");
        else {
            if (pressed & plat::kL) g_tab_request.tab((g_tab + menu_tab_count() - 1) % menu_tab_count(), g_navigation_frame.state.generation);
            if (pressed & plat::kR) g_tab_request.tab((g_tab + 1) % menu_tab_count(), g_navigation_frame.state.generation);
        }
    }
    feed_pad(io);
    if (g_input_frames > 0) g_input_frames--;

    ImGui::NewFrame();
    bool open = g_pad_gate.is_open();
    static bool was_open = false;
    const bool appearing = open && !was_open;
    was_open = open;
    // Quick settings is a frequent keyboard/controller action: show its final
    // layout immediately rather than delaying input feedback behind a fade.
    const float reveal = open ? 1.f : 0.f;
    g_fading = false;
    const ScreenFn screen = g_screen.load();
    if (screen) screen(float(w), float(h));  // under the menu, which the screen can open (its Settings)
    if (progress_wanted()) draw_progress(float(h));
    if (g_settings.metrics_enabled && !progress_wanted() && !screen) draw_metrics(float(h));  // (no game frames to measure yet)
    if (open || reveal > 0.0f) {
        const float a = ui::smoothstep(reveal);
        ImGui::GetBackgroundDrawList()->AddRectFilled({0, 0}, io.DisplaySize, IM_COL32(4, 2, 10, int(120 * a)));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a);
        bool still_open = open;
        const auto route = open ? g_tab_request.take(g_pad_gate.generation()) : MenuRoute{};
        MenuContext c{g_settings, g_host, g_capture, route.tab, still_open, scale,
                      appearing, float(h), reveal, game_started ? &startup_settings : nullptr, route.controls_section};
        draw_menu(c);
        ImGui::PopStyleVar();
        if (open && c.tab_request >= 0 && c.tab_request != g_tab) log("overlay: tab %d", c.tab_request);
        if (open && c.tab_request >= 0) g_tab = c.tab_request;
        if (open && !still_open) set_open(false, "menu"), g_capture = -1;
    }
    quarantine_changed_controller_bindings();
    // A remap may start or finish inside draw_menu, after the pre-frame poll.
    // Arm the barrier here so cached neutral cannot release a held binding.
    g_pad_gate.set_capture(g_capture >= 0 || controller_remap_state().capture >= 0);
    ui::draw_notices(float(h));
    ImGui::Render();
    return ImGui::GetDrawData();
}

}  // namespace nx::overlay
