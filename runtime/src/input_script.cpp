// Scripted input for unattended runs: NX_INPUT_SCRIPT="<seconds>:<button>:<hold seconds>;..." presses buttons at
// fixed times after the first pad read, e.g. "70:A:0.2;74:A:0.2;80:RIGHT:5" (buttons: A B X Y L R ZL ZR PLUS MINUS
// LEFT RIGHT UP DOWN; the directions also push the left stick). Added on top of the real pad and keyboard.
//
// NX_INPUT_RECORD=1: what the player does is written in that same format, to <data root>/input_record.txt
// (a path instead is the frame recorder's, play.cpp): every press of a button, and the stick past half way as a direction, from the first
// pad read. Played back through NX_INPUT_SCRIPT, a test run follows a real player's run instead of a blind script.
#include <cstdio>
#include <array>
#include <mutex>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <nx/runtime.h>

#include "platform.h"

namespace nx::plat {

namespace {

struct Press {
    double at = 0, hold = 0;
    uint64_t bits = 0;
    int lx = 0, ly = 0;
};

std::vector<Press> parse(const char* s) {
    struct Name {
        const char* name;
        uint64_t bits;
        int lx, ly;
    };
    static const Name names[] = {
        {"A", kA, 0, 0},          {"B", kB, 0, 0},          {"X", kX, 0, 0},          {"Y", kY, 0, 0},
        {"L", kL, 0, 0},          {"R", kR, 0, 0},          {"ZL", kZL, 0, 0},        {"ZR", kZR, 0, 0},
        {"PLUS", kPlus, 0, 0},    {"MINUS", kMinus, 0, 0},  {"LEFT", kLeft, -kStickMax, 0},
        {"RIGHT", kRight, kStickMax, 0}, {"UP", kUp, 0, kStickMax}, {"DOWN", kDown, 0, -kStickMax},
        {"DLEFT", kLeft, 0, 0}, {"DRIGHT", kRight, 0, 0}, {"DUP", kUp, 0, 0}, {"DDOWN", kDown, 0, 0},
    };
    std::vector<Press> v;
    std::string all = s;
    size_t pos = 0;
    while (pos < all.size()) {
        size_t end = all.find(';', pos);
        if (end == std::string::npos) end = all.size();
        std::string item = all.substr(pos, end - pos);
        pos = end + 1;
        size_t a = item.find(':'), b = item.find(':', a + 1);
        if (a == std::string::npos || b == std::string::npos) continue;
        Press p;
        p.at = std::atof(item.substr(0, a).c_str());
        p.hold = std::atof(item.substr(b + 1).c_str());
        std::string button = item.substr(a + 1, b - a - 1);
        for (size_t from = 0; from < button.size();) {
            const auto to = button.find('+',from);
            const auto part = button.substr(from,to == std::string::npos ? to : to-from);
            for (const Name& n : names) if (part == n.name) {
                p.bits |= n.bits;
                if (n.lx) p.lx = n.lx;
                if (n.ly) p.ly = n.ly;
            }
            if (to == std::string::npos) break;
            from = to+1;
        }
        if (p.bits) v.push_back(p);
    }
    return v;
}

// The recorder: per button, when it went down; a release writes "<down>:<name>:<held>;" (flushed at once, so a
// killed app keeps what was played).
void record(const PadState& p, double t) {
    static const char* names[] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR", "PLUS", "MINUS", "LEFT", "RIGHT", "UP", "DOWN"};
    static const uint64_t bits[] = {kA, kB, kX, kY, kL, kR, kZL, kZR, kPlus, kMinus, kLeft, kRight, kUp, kDown};
    static double down[14];
    static bool init = false;
    static FILE* f = nullptr;
    if (!init) {
        init = true;
        for (double& d : down) d = -1;
        f = fopen_utf8(data_root() + "/input_record.txt", "w");
    }
    if (!f) return;
    constexpr int32_t half = kStickMax / 2;
    uint64_t now = p.buttons;
    if (p.lx < -half) now |= kLeft;
    if (p.lx > half) now |= kRight;
    if (p.ly > half) now |= kUp;
    if (p.ly < -half) now |= kDown;
    for (int k = 0; k < 14; k++) {
        bool held = now & bits[k];
        if (held && down[k] < 0) down[k] = t;
        if (!held && down[k] >= 0) {
            std::fprintf(f, "%.2f:%s:%.2f;", down[k], names[k], std::max(0.05, t - down[k]));
            std::fflush(f);
            down[k] = -1;
        }
    }
}

}  // namespace

void input_script(PadState& p, uint32_t player) {
    if (player >= 4) return;
    struct State {
        bool initialized = false, have_file = false;
        const char* env = nullptr;
        uint64_t start = 0, script_start = 0, next_poll = 0;
        std::vector<Press> script;
        std::string previous;
    };
    static std::array<State, 4> states;
    static std::mutex lock;
    std::lock_guard guard(lock);
    auto& state = states[player];
    if (!state.initialized) {
        char name[32];
        std::snprintf(name, sizeof name, "NX_INPUT_SCRIPT_P%u", player + 1);
        state.env = std::getenv(player == 0 ? "NX_INPUT_SCRIPT" : name);
        state.start = state.script_start = monotonic_ns();
        state.initialized = true;
        if (state.env) state.script = parse(state.env[0] == '@' ? "" : state.env);
    }
    const char* env = state.env;
    // Only NX_INPUT_RECORD=1: a path is the frame recorder's (play.cpp), which must not share its file.
    static const bool recording = [] {
        const char* e = std::getenv("NX_INPUT_RECORD");
        return e && std::strcmp(e, "1") == 0;
    }();
    if (!env && !(player == 0 && recording)) return;
    const uint64_t start = state.start;
    double t = double(monotonic_ns() - start) / 1e9;
    if (player == 0 && recording) record(p, t);  // what the player did, before the script adds to it
    if (!env) return;
    // @file: a developer can replace the short script while a GPU repro is running. Each
    // changed file starts its own relative clock; recording keeps the session's original clock.
    auto& script = state.script; auto& previous = state.previous;
    auto& have_file = state.have_file;
    auto& script_start = state.script_start; auto& next_poll = state.next_poll;
    if (env[0] == '@') {
        const uint64_t now = monotonic_ns();
        if (now >= next_poll) {
            next_poll = now + 250'000'000;
            if (FILE* f = fopen_utf8(env + 1, "rb")) {
                char bytes[65537];
                const size_t n = std::fread(bytes, 1, sizeof bytes, f);
                const bool ok = !std::ferror(f) && n < sizeof bytes;
                std::fclose(f);
                if (ok) {
                    std::string text(bytes, n);
                    if (!have_file || text != previous) {
                        script = parse(text.c_str());
                        previous = std::move(text), have_file = true, script_start = now;
                        log("input: live script replaced (%zu presses)", script.size());
                    }
                }
            }
        }
        t = double(now - script_start) / 1e9;
    }
    for (const Press& s : script)
        if (t >= s.at && t < s.at + s.hold) {
            p.buttons |= s.bits;
            if (s.lx) p.lx = s.lx;
            if (s.ly) p.ly = s.ly;
        }
}

}  // namespace nx::plat
