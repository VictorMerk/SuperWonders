#include "button_font.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>

static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
static uint32_t u32(const uint8_t* p) { return uint32_t(p[0])<<24 | uint32_t(p[1])<<16 | uint32_t(p[2])<<8 | p[3]; }
static uint32_t glyph(const std::vector<uint8_t>& f, uint32_t cp) {
    const uint32_t n = uint32_t(f[4])<<8 | f[5];
    for (uint32_t i = 0; i < n; ++i) {
        const auto* r = f.data()+12+i*16;
        if (u32(r) != 0x636d6170) continue;
        const auto* c = f.data()+u32(r+8);
        const auto* sub = c+u32(c+8);
        for (uint32_t j = 0; j < u32(sub+12); ++j) {
            const auto* g = sub+16+j*12;
            if (u32(g) == cp) return u32(g+8);
        }
    }
    return 0;
}
int main(int argc, char** argv) {
    using namespace nx;
    using plat::ControllerKind;
    overlay::Settings s;
    for (auto kind : {ControllerKind::Nintendo, ControllerKind::Xbox, ControllerKind::PlayStation}) {
        auto f = pc_button_font(s, kind);
        check(!f.empty(), "embedded font exists");
        uint32_t sum = 0;
        for (size_t i = 0; i < f.size(); i += 4) {
            uint32_t w = 0;
            for (size_t j = 0; j < 4; ++j) w = (w<<8) | (i+j < f.size() ? f[i+j] : 0);
            sum += w;
        }
        check(sum == 0xB1B0AFBA, "SFNT checksum after remapping");
        check(glyph(f, 0xE0E0) == glyph(f, 0xE201), "Nintendo layout A uses the physical east button");
        check(glyph(f, 0xE0A0) == glyph(f, 0xE0E0), "old/new prompt aliases agree");
        s.controller_custom_mask = 1; s.controller_bindings[0] = 7;
        auto custom = pc_button_font(s, kind);
        check(glyph(custom, 0xE0E0) == glyph(custom, 0xE207), "custom binding redraws A as the right trigger");
        s.controller_custom_mask = 0;
    }
    std::array<std::string, 16> keys{}; keys[0] = "Space"; keys[4] = "Q";
    s.keyboard_actions=false;
#ifdef _WIN32
    s.keyboard_bindings[0]=32;s.keyboard_bindings[4]='Q';
#endif
    auto keyboard = pc_button_font(s, ControllerKind::Keyboard, keys);
    check(glyph(keyboard, 0xE0E0) == glyph(keyboard, 0xE400), "keyboard Space keycap");
    check(glyph(keyboard, 0xE0E4) == glyph(keyboard, 0xE351), "keyboard custom letter keycap");
    s.keyboard_actions=true;s.keyboard_game_layout=1;
    s.keyboard_bindings=input::recommended_keyboard_bindings();
    keys[0]="F";keys[1]="Space";keys[2]="Q";keys[3]="Shift";keys[5]="E";
    auto alternate=pc_button_font(s,ControllerKind::Keyboard,keys);
    check(glyph(alternate,0xE0E0)==glyph(alternate,0xE400),"alternate game A still shows the Space jump key");
    check(glyph(alternate,0xE0E1)==glyph(alternate,0xE401),"alternate game B shows the Shift sprint key");
    check(glyph(alternate,0xE0E2)==glyph(alternate,0xE346),"alternate game X shows F for use/confirm");
    check(glyph(alternate,0xE0E7)==glyph(alternate,0xE345),"both spin prompts show the dedicated E key");
    s.button_icons = 4;
    auto forced = pc_button_font(s, ControllerKind::Keyboard, keys);
    check(forced == pc_button_font(s, ControllerKind::PlayStation, keys), "manual prompt device override");
    if (argc > 1) {
        std::ofstream f(argv[1], std::ios::binary);
        f.write(reinterpret_cast<const char*>(forced.data()), forced.size());
    }
    std::puts("button fonts: controller bindings, aliases, keyboard and checksums passed");
}
