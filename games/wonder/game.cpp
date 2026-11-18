// Super Mario Bros. Wonder (010015100B514000, 1.0.0): what the shared runtime needs to know about the game.
// The game draws through NVN (docs/NVN.md); what we know about it is in docs/nvn/WONDER.md.
#include <nx/game.h>
#include <nx/runtime.h>
#include <nx/gamelib.h>
#include <nx/module.h>

#include <cstring>
#include <cstdlib>
#include <atomic>
#include <algorithm>
#include <cmath>

#include "platform.h"
#include "netplay_game.h"
#include "nvn/nvn_ultrawide.h"

namespace nx {

namespace nvn { void hold_scene_frame_cap(int fps); }

void game::file_opened(const char* path) {
    if(lan::active()) {
        const char* stream=std::strstr(path,"/Sound/Resource/Stream/BGM_");
        if(stream && (std::strstr(stream,"BGM_Title") || std::strstr(stream,"BGM_World_") || std::strstr(stream,"BGM_Course")))
            lan::loading_event(path);
    }
    static std::atomic<bool> held{false};
    // The map can remain resident while entering a course, so returning to it does not necessarily reopen its
    // StageLoadInfo. Its world music does reopen when the player returns from this house.
    if (held.load() && std::strstr(path, "/Sound/Resource/Stream/BGM_World_") && held.exchange(false)) {
        nvn::hold_scene_frame_cap(0);
        log("wonder: first house scene left for world map: selected frame cap restored");
        return;
    }
    constexpr const char* prefix = "/Stage/StageLoadInfo/";
    const char* scene = std::strstr(path, prefix);
    if (!scene) return;
    scene += std::strlen(prefix);
    constexpr const char* house = "Course500_Course.game__stage__StageLoadInfo.bgyml";
    const bool in_house = std::strcmp(scene, house) == 0;
    if (held.exchange(in_house) != in_house) {
        nvn::hold_scene_frame_cap(in_house ? 60 : 0);
        log("wonder: first house scene %s: %s", in_house ? "loading" : "left",
            in_house ? "frame cap held at 60 for dialogue compatibility" : "selected frame cap restored");
    }
}

const game::Info game::kInfo = {"wonder", "Super Mario Bros. Wonder", 0x010015100B514000ull,
                                "io.github.belmantegu.wonderrecomp", "private/wonder",
                                0x1000000 /* main.npdm: 16 MiB system resource */};

namespace {

std::atomic<uint64_t> g_wide_calls{0};
Fn g_wide_original = nullptr;
Fn g_ortho_original = nullptr;
Fn g_camera_entry_original = nullptr;
Fn g_active_rect_original = nullptr;
std::atomic<unsigned> g_active_rect_count{0};
std::atomic<unsigned> g_camera_entry_calls{0};
std::atomic<unsigned> g_ortho_samples{0};
std::atomic<unsigned> g_ortho_adjustments{0};
static uint64_t frame_step_page();

void active_rect_hook(CPU& c) {
    const ModuleInfo* main = game_lib().main;
    static const uint64_t chosen_caller = [] {
        const char* v = std::getenv("NX_UW_RECT_CALLER");
        return v ? std::strtoull(v, nullptr, 16) : uint64_t(0x6caba4);
    }();
    static const bool candidate_group = [] {
        const char* v = std::getenv("NX_UW_RECT_GROUP_TEST");
        return v && std::strcmp(v, "1") == 0;
    }();
    static const bool candidate_all = [] {
        const char* v = std::getenv("NX_UW_RECT_ALL_TEST");
        return v && std::strcmp(v, "1") == 0;
    }();
    const uint64_t rel = main && c.x[30] >= main->base ? c.x[30] - main->base : 0;
    const bool relevant = main && (candidate_all || rel == chosen_caller || (candidate_group &&
        (rel == 0x1d1f50 || rel == 0x441328 || rel == 0x82d62c || rel == 0x3293a8 ||
         rel == 0x329008 || rel == 0x49b224)));
    const uint64_t caller = c.x[30];
    const uint64_t result = c.x[0];
    g_active_rect_original(c);
    static const bool rect_trace = std::getenv("NX_UW_RECT_TRACE") &&
                                   std::strcmp(std::getenv("NX_UW_RECT_TRACE"), "1") == 0;
    if (rect_trace && main && result && ld<uint8_t>(result + 0x10)) {
        static std::atomic<uint64_t> seen[64]{};
        bool first = false;
        for (auto& slot : seen) {
            uint64_t value = slot.load(std::memory_order_relaxed);
            if (value == caller) break;
            if (!value && slot.compare_exchange_strong(value, caller, std::memory_order_relaxed)) {
                first = true;
                break;
            }
            if (value == caller) break;
        }
        const bool valid = ld<uint8_t>(result + 0x10) != 0;
        if (first) log("widescreen rect trace: caller=main+%llx valid=%u bounds=%g,%g,%g,%g",
            (unsigned long long)(caller - main->base), unsigned(valid), double(ld<float>(result)),
            double(ld<float>(result + 4)), double(ld<float>(result + 8)), double(ld<float>(result + 12)));
    }
    static const bool active = std::getenv("NX_UW_ACTIVE_AREA_TEST") &&
                               std::strcmp(std::getenv("NX_UW_ACTIVE_AREA_TEST"), "1") == 0;
    if (!active) return;
    if (!relevant || !result || !ld<uint8_t>(result + 0x10)) return;
    const float left = ld<float>(result), right = ld<float>(result + 8);
    const float factor = nvn::uw_factor();
    if (!std::isfinite(left) || !std::isfinite(right) || right <= left || factor <= 1.f) return;
    const float extra = (right - left) * (factor - 1.f) * 0.5f;
    if (!std::isfinite(extra) || extra > 10000.f) return;
    st<float>(result, left - extra);
    st<float>(result + 8, right + extra);
    if (g_active_rect_count.fetch_add(1, std::memory_order_relaxed) < (candidate_all ? 32u : 8u))
        log("widescreen active-area test: caller=main+%llx index=%u bounds=%.3f,%.3f..%.3f,%.3f -> x %.3f..%.3f",
            (unsigned long long)rel, unsigned(c.x[1]), double(left), double(ld<float>(result + 4)),
            double(right), double(ld<float>(result + 12)), double(left - extra), double(right + extra));
}

static bool horplus_enabled() {
    const char* value = std::getenv("NX_WONDER_HORPLUS");
    return value && value[0] && std::strcmp(value, "0") != 0;
}

static float horplus_aspect() {
    const uint64_t page = frame_step_page();
    if (!page) return 16.0f / 9.0f;
    return ld<float>(page + 8);
}

void ortho_camera_hook(CPU& c) {
    const unsigned sample = g_ortho_samples.fetch_add(1, std::memory_order_relaxed);
    const uint64_t p = c.x[0];
    const float top = ld<float>(p + 0xa0), bottom = ld<float>(p + 0xa4);
    float left = ld<float>(p + 0xa8), right = ld<float>(p + 0xac);
    const float current_aspect = (right - left) / std::abs(top - bottom);
    const float target_aspect = nvn::uw_on() ? (16.0f / 9.0f) * nvn::uw_factor() : horplus_aspect();
    const ModuleInfo* main = game_lib().main;
    const bool world_camera = main && c.x[30] == main->base + 0x1a95e44;
    if (((nvn::uw_on() && world_camera) || horplus_enabled()) &&
        std::isfinite(current_aspect) && std::isfinite(target_aspect) &&
        current_aspect > 1.76f && current_aspect < 1.80f && target_aspect > current_aspect + 0.01f &&
        target_aspect < 4.1f) {
        const float center = (left + right) * 0.5f;
        const float half_width = std::abs(top - bottom) * target_aspect * 0.5f;
        left = center - half_width;
        right = center + half_width;
        st<float>(p + 0xa8, left);
        st<float>(p + 0xac, right);
        const unsigned adjusted = g_ortho_adjustments.fetch_add(1, std::memory_order_relaxed);
        if (adjusted < 32)
            log("widescreen ortho-camera: adjusted caller=%llx aspect %.5f -> %.5f bounds %.4f..%.4f",
                (unsigned long long)c.x[30], double(current_aspect), double(target_aspect), double(left), double(right));
    } else if (plat::setting("wonder_widescreen_trace") == "1" && (sample < 32 || (sample % 5000) == 0)) {
        log("widescreen ortho-camera: object=%llx caller=%llx left=%.4f right=%.4f top=%.4f bottom=%.4f",
            (unsigned long long)p, (unsigned long long)c.x[30], double(left), double(right), double(top), double(bottom));
    }
    g_ortho_original(c);
}

void wide_probe(CPU& c) {
    g_wide_calls.fetch_add(1, std::memory_order_relaxed);
    g_wide_original(c);
}

void camera_entry_probe(CPU& c) {
    unsigned n = g_camera_entry_calls.fetch_add(1, std::memory_order_relaxed);
    if (n < 16 || n % 10000 == 0)
        log("widescreen camera-entry: count=%u caller=%llx object=%llx runtime-aspect=%.5f", n + 1,
            (unsigned long long)c.x[30], (unsigned long long)c.x[0], double(horplus_aspect()));
    g_camera_entry_original(c);
}

void install_wide_probe() {
    const bool trace = plat::setting("wonder_widescreen_trace") == "1";
    const char* uw = std::getenv("NX_ULTRAWIDE");
    if (!trace && !horplus_enabled() && !(uw && uw[0] && uw[0] != '0')) return;
    const ModuleInfo* main = game_lib().main;
    if (!main) return;
    constexpr uint64_t offset = 0x1c34328;
    constexpr uint32_t code[] = {0xfc1b0fee, 0x6d00b3ed, 0xf90027f3, 0x6d01abeb,
                                 0x6d02a3e9, 0xa903fbfd, 0x9100e3fd, 0xb000cfc8};
    const LoadedModule* module = module_at(main->base);
    if (!module) return;
    const char* active_test = std::getenv("NX_UW_ACTIVE_AREA_TEST");
    const char* rect_trace = std::getenv("NX_UW_RECT_TRACE");
    if ((active_test && std::strcmp(active_test, "1") == 0) ||
        (rect_trace && std::strcmp(rect_trace, "1") == 0)) {
        constexpr uint64_t fn = 0x1d6610;
        constexpr uint32_t words[] = {0x2a2103e8, 0x3900401f, 0x72001d1f, 0xa9007c1f};
        bool ok = ld<uint32_t>(main->base + 0x6caba0) == 0x97ec2e9c;
        for (unsigned i = 0; i < 4; ++i) ok &= ld<uint32_t>(main->base + fn + 4 * i) == words[i];
        if (ok && module->table[fn / 4]) {
            g_active_rect_original = module->table[fn / 4];
            module->table[fn / 4] = active_rect_hook;
            log("widescreen active-area test: hook installed at main+1d6610");
        } else log("widescreen active-area test: incompatible code or unavailable");
    }
    constexpr uint64_t camera_entry = 0x160de38;
    if (trace && ld<uint32_t>(main->base + camera_entry) == 0x14000003 && module->table[camera_entry / 4]) {
        g_camera_entry_original = module->table[camera_entry / 4];
        module->table[camera_entry / 4] = camera_entry_probe;
        log("widescreen camera-entry: probe installed at main+160de38");
    }
    constexpr uint64_t ortho = 0x10578c;
    constexpr uint32_t ortho_code[] = {0x2d550400, 0x1e3c1003, 0x1e2e1004, 0xd2e7f008};
    bool compatible = true;
    for (unsigned i = 0; i < 4; ++i)
        compatible &= ld<uint32_t>(main->base + ortho + 4 * i) == ortho_code[i];
    if (compatible && module->table[ortho / 4]) {
        g_ortho_original = module->table[ortho / 4];
        module->table[ortho / 4] = ortho_camera_hook;
        log("widescreen ortho-camera: %s hook installed at main+10578c",
            (horplus_enabled() || (uw && uw[0] && uw[0] != '0')) ? "ultrawide" : "trace");
    } else {
        log("widescreen ortho-camera: incompatible code or unavailable; probe disabled");
    }
    if (!trace) return;
    for (unsigned i = 0; i < 8; ++i) {
        if (ld<uint32_t>(main->base + offset + 4 * i) != code[i]) {
            log("widescreen calls: incompatible game code; probe disabled");
            return;
        }
    }
    g_wide_original = module->table[offset / 4];
    if (!g_wide_original) {
        log("widescreen calls: function unavailable; probe disabled");
        return;
    }
    module->table[offset / 4] = wide_probe;
    log("widescreen calls: probe installed at main+%llx", (unsigned long long)offset);
}

static uint32_t float_bits(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint64_t frame_step_page() {
    const ModuleInfo* main = game_lib().main;
    if (!main || !main->frame_step_offset) return 0;
    return main->base + main->frame_step_offset;
}

static void store_frame_step(float seconds) {
    const uint64_t page = frame_step_page();
    if (!page) return;
    const float ratio = seconds * 60.0f;
    uint64_t bits = 0;
    std::memcpy(&bits, &ratio, sizeof ratio);
    std::memcpy(reinterpret_cast<uint8_t*>(&bits) + sizeof ratio, &seconds, sizeof seconds);
    auto* step = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(page));
    std::atomic_ref<uint64_t>(*step).store(bits, std::memory_order_release);
}

static void update_wide_aspect() {
    static const bool horplus = [] {
        const char* value = std::getenv("NX_WONDER_HORPLUS");
        return value && value[0] && std::strcmp(value, "0") != 0;
    }();
    static uint32_t last_width = 0, last_height = 0;
    uint32_t width = 1920, height = 1080;
    if (horplus) plat::window_size(width, height);
    if (!width || !height) width = 1920, height = 1080;
    if (width == last_width && height == last_height) return;

    const uint64_t page = frame_step_page();
    if (!page) return;
    const float aspect = horplus ? float(width) / float(height) : (16.0f / 9.0f);
    auto* aspect_bits = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(page + 8));
    std::atomic_ref<uint32_t>(*aspect_bits).store(float_bits(aspect), std::memory_order_release);
    last_width = width;
    last_height = height;
    if (horplus)
        log("widescreen perspective: runtime camera aspect %.5f from window %u x %u", double(aspect), width, height);
}

static void init_frame_step() {
    if (!frame_step_page()) {
        log("wonder: real-time frame step page was not reserved; retaining the game's original frame timing");
        return;
    }
    const auto rate=lan::active()?lan::simulation_rate():60;
    store_frame_step(1.0f / float(rate));
    update_wide_aspect();
    plat::overlay_game_speed(100.0);
    log("wonder: real-time frame step initialized at %.2f ms",1000.0/double(rate));
}

// Gather evidence about Wonder's own virtual-screen globals before implementing Hor+.
// The final image pass can only fit, crop, or stretch its 16:9 source; finding and
// classifying these game-side values is needed to expand the camera while preserving HUD layout.
static void trace_screen_globals() {
    if (plat::setting("wonder_widescreen_trace") != "1") return;
    const ModuleInfo& nx_module_main = *game_lib().main;
    const uint64_t begin = nx_module_main.base + nx_module_main.data;
    const uint64_t end = nx_module_main.base + nx_module_main.bss + nx_module_main.bss_size;
    const uint32_t pairs[][2] = {
        {1920, 1080}, {1280, 720}, {960, 540}, {640, 360},
        {float_bits(1920.0f), float_bits(1080.0f)},
        {float_bits(1280.0f), float_bits(720.0f)},
        {float_bits(960.0f), float_bits(540.0f)},
        {float_bits(640.0f), float_bits(360.0f)},
    };
    constexpr const char* names[] = {"1920x1080 ints", "1280x720 ints", "960x540 ints", "640x360 ints",
                                    "1920x1080 floats", "1280x720 floats", "960x540 floats", "640x360 floats"};
    log("widescreen probe: scanning main data+bss [%llx,%llx)", (unsigned long long)begin,
        (unsigned long long)end);
    for (size_t p = 0; p < sizeof(pairs) / sizeof(pairs[0]); ++p) {
        unsigned count = 0;
        for (uint64_t address = begin; address + 8 <= end; address += 4) {
            const auto* words = reinterpret_cast<const uint32_t*>(address);
            if (words[0] != pairs[p][0] || words[1] != pairs[p][1]) continue;
            if (count < 24)
                log("widescreen probe: %s candidate main+%llx", names[p],
                    (unsigned long long)(address - nx_module_main.base));
            ++count;
        }
        log("widescreen probe: %s count=%u", names[p], count);
    }
}

}  // namespace

namespace nvn {
int held_frame_cap();
}

namespace wonder {
void lan_init();
void menu_init();
void seed_guard_init();
void seed_guard_tick(uint64_t now_ns);
}

void game::after_init() {
    const auto* source = game_lib().main;
    log("wonder: source NSO build %s", source && source->build_id ? source->build_id : "unknown");
    init_frame_step();
    trace_screen_globals();
    install_wide_probe();
    wonder::menu_init();
    wonder::lan_init();
    if (!lan::active()) wonder::seed_guard_init();
}
void game::renderer_start() {}
void game::first_frame() {}

void game::frame_presented(uint64_t now_ns, int present_interval) {
    if (lan::active()) {
        const auto rate=lan::simulation_rate();
        store_frame_step(1.0f/float(rate));
        static uint64_t from = 0, frames = 0;
        if (!from) { from=now_ns; frames=0; return; }
        if (++frames >= rate && now_ns > from) {
            const double speed=double(frames)*1e9/double(rate)/double(now_ns-from)*100.0;
            plat::overlay_game_speed(speed);
            static unsigned reports=0;
            if (++reports%10==0) log("wonder: LAN target %u Hz, observed game speed %.1f%%",unsigned(rate),speed);
            from = now_ns; frames = 0;
        }
        return;
    }
    if (plat::setting("wonder_widescreen_trace") == "1") {
        static uint64_t next_trace = 0;
        if (now_ns >= next_trace) {
            next_trace = now_ns + 10'000'000'000ull;
            log("widescreen calls: main+1c34328 last 10s = %llu",
                (unsigned long long)g_wide_calls.exchange(0, std::memory_order_relaxed));
            const ModuleInfo* module = game_lib().main;
            constexpr uint64_t offsets[] = {0x362db38, 0x362dbb0, 0x362dc00, 0x362dc50};
            if (module) for (uint64_t offset : offsets) {
                if (offset + 8 > module->bss + module->bss_size) continue;
                float pair[2];
                std::memcpy(pair, reinterpret_cast<const void*>(module->base + offset), sizeof pair);
                log("widescreen live: main+%llx = %.3f x %.3f",
                    (unsigned long long)offset, pair[0], pair[1]);
            }
        }
    }
    // recomp/wonder_realtime.py reserves one aligned page at the end of main's BSS.
    // Two adjacent floats are consumed by the patched game code: seconds * 60 and seconds.
    static const bool realtime = [] {
        const char* value = std::getenv("NX_REALTIME_FRAME_STEP");
        return !(value && value[0] == '0');
    }();
    static uint64_t previous_ns = 0;
    static double samples[5]{};
    static unsigned sample_count = 0, sample_next = 0;
    static uint64_t report_ns = 0, report_frames = 0;
    static double report_seconds = 0, report_game_seconds = 0;
    static double speed_elapsed = 0, speed_simulated = 0;

    if (!frame_step_page()) return;
    update_wide_aspect();

    if (!previous_ns) {
        previous_ns = now_ns;
        report_ns = now_ns;
        return;
    }
    double dt = double(now_ns - previous_ns) * 1e-9;
    previous_ns = now_ns;
    if (dt < 0.001 || dt > 0.25) {
        sample_count = sample_next = 0;
        return;
    }

    double seconds = double(std::max(1, present_interval)) / 60.0;
    wonder::seed_guard_tick(now_ns);
    if (realtime && nvn::held_frame_cap() == 60) {
        // A moment the game needs at its own rate (seed_guard.cpp): presented at 60, a 60 Hz step each frame; the
        // average starts again when the hold ends.
        seconds = 1.0 / 60.0;
        sample_count = sample_next = 0;
    } else if (realtime) {
        samples[sample_next++ % 5] = dt;
        if (sample_count < 5) ++sample_count;
        seconds = 0;
        for (unsigned i = 0; i < sample_count; ++i) seconds += samples[i];
        seconds /= sample_count;
        seconds = std::min(seconds, 1.0 / 15.0);  // spread brief hitches; avoid an enormous one-frame simulation step
    }

    store_frame_step(float(seconds));
    speed_elapsed += dt;
    speed_simulated += seconds;
    if (speed_elapsed >= 0.5) {
        plat::overlay_game_speed(speed_elapsed > 0 ? speed_simulated / speed_elapsed * 100.0 : 100.0);
        speed_elapsed = speed_simulated = 0;
    }

    ++report_frames;
    report_seconds += dt;
    report_game_seconds += seconds;
    if (now_ns - report_ns >= 30'000'000'000ull) {
        log("wonder: real-time frame step %s: %llu presents in %.1f s (%.1f fps), simulation step %.4f of elapsed time",
            realtime ? "enabled" : "disabled", (unsigned long long)report_frames,
            double(now_ns - report_ns) / 1e9, double(report_frames) / (double(now_ns - report_ns) / 1e9),
            report_seconds > 0 ? report_game_seconds / report_seconds : 0.0);
        report_ns = now_ns;
        report_frames = 0;
        report_seconds = report_game_seconds = 0;
    }
}

}  // namespace nx
