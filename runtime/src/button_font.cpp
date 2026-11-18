// Original vector icons built from bundled OFL Nunito. No Nintendo font/atlas is included.
#include "button_font.h"
#include <algorithm>
#include <cstring>
#include <cctype>

namespace nx {
namespace {
#include "pc_button_fonts.inc"
uint32_t be32(const uint8_t* p) { return uint32_t(p[0])<<24 | uint32_t(p[1])<<16 | uint32_t(p[2])<<8 | p[3]; }
uint16_t be16(const uint8_t* p) { return uint16_t(p[0])<<8 | p[1]; }
void put32(uint8_t* p, uint32_t v) { for (int i = 3; i >= 0; --i) { p[i] = uint8_t(v); v >>= 8; } }
uint32_t checksum(const uint8_t* p, size_t size) {
    uint32_t sum = 0;
    for (size_t i = 0; i < size; i += 4) {
        uint32_t word = 0;
        for (size_t k = 0; k < 4; ++k) word = (word << 8) | (i+k < size ? p[i+k] : 0);
        sum += word;
    }
    return sum;
}
const char* const extra_keys[] = {"SPACE","SHIFT","CTRL","ALT","ENTER","ESC","TAB","BACK",
    "LEFT","UP","RIGHT","DOWN","HOME","END","PGUP","PGDN","INS","DEL",
    "F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};
uint32_t key_code(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    if (name.size() == 1 && name[0] > 32 && name[0] < 127) return 0xE300 + uint8_t(name[0]);
    if (name == "RETURN") name = "ENTER";
    if (name == "ESCAPE") name = "ESC";
    if (name == "CONTROL" || name == "LEFT CTRL" || name == "RIGHT CTRL") name = "CTRL";
    if (name == "LEFT SHIFT" || name == "RIGHT SHIFT") name = "SHIFT";
    if (name == "LEFT ALT" || name == "RIGHT ALT") name = "ALT";
    if (name == "BACKSPACE") name = "BACK";
    if (name == "PAGEUP" || name == "PAGE UP") name = "PGUP";
    if (name == "PAGEDOWN" || name == "PAGE DOWN") name = "PGDN";
    if (name == "INSERT") name = "INS";
    if (name == "DELETE") name = "DEL";
    for (size_t i = 0; i < std::size(extra_keys); ++i) if (name == extra_keys[i]) return 0xE400 + uint32_t(i);
    return 0;
}
uint32_t binding_key_code(const overlay::Settings& s, size_t action, const std::string& label) {
#ifdef _WIN32
    // Windows key names follow the user's OS language; virtual-key values do not.
    const int vk = s.keyboard_bindings[action];
    static constexpr int special[] = {32,16,17,18,13,27,9,8,37,38,39,40,36,35,33,34,45,46};
    for (size_t i = 0; i < std::size(special); ++i) if (vk == special[i]) return 0xE400 + uint32_t(i);
    if (vk >= 112 && vk <= 123) return 0xE400 + 18 + uint32_t(vk-112);
#endif
    return key_code(label);
}
}

const char* physical_button_name(plat::ControllerKind kind, int physical) {
    static constexpr const char* names[3][16] = {
        {"B","A","Y","X","L","R","ZL","ZR","+","-","L Stick","R Stick","Left","Up","Right","Down"},
        {"A","B","X","Y","LB","RB","LT","RT","Menu","View","LS","RS","Left","Up","Right","Down"},
        {"Cross","Circle","Square","Triangle","L1","R1","L2","R2","Options","Create / Share","L3","R3","Left","Up","Right","Down"}};
    const int device = kind == plat::ControllerKind::Nintendo ? 0 : kind == plat::ControllerKind::PlayStation ? 2 : 1;
    return names[device][std::clamp(physical, 0, 15)];
}

std::vector<uint8_t> pc_button_font(const overlay::Settings& s, plat::ControllerKind kind,
                                  const std::array<std::string, 16>& keys) {
    if (s.button_icons) kind = static_cast<plat::ControllerKind>(s.button_icons - 1);
    // Setting order: Auto, Keyboard, Nintendo, Xbox, PlayStation (ControllerKind + 1).
    const uint8_t* bytes = nintendo;
    size_t size = sizeof nintendo;
    if (kind == plat::ControllerKind::Xbox) bytes = xbox, size = sizeof xbox;
    if (kind == plat::ControllerKind::PlayStation) bytes = playstation, size = sizeof playstation;
    std::vector<uint8_t> out(bytes, bytes + size);
    uint8_t *cmap_record = nullptr, *head = nullptr, *cmap = nullptr;
    uint32_t cmap_size = 0;
    for (uint32_t i = 0; i < be16(out.data()+4); ++i) {
        auto* r = out.data()+12+16*i;
        if (std::memcmp(r, "head", 4) == 0) head = out.data()+be32(r+8);
        if (std::memcmp(r, "cmap", 4) == 0) {
            cmap_record = r; cmap = out.data()+be32(r+8); cmap_size = be32(r+12);
        }
    }
    if (!head || !cmap || cmap_size < 28) return {};
    auto* sub = cmap + be32(cmap+8);
    const uint32_t count = be32(sub+12);
    auto glyph = [&](uint32_t cp) -> uint32_t {
        for (uint32_t i = 0; i < count; ++i) {
            const auto* g = sub+16+i*12;
            if (be32(g) == cp && be32(g+4) == cp) return be32(g+8);
        }
        return 0;
    };
    auto replace = [&](uint32_t cp, uint32_t target) {
        const uint32_t id = glyph(target);
        if (!id) return;  // an unknown keyboard key keeps a readable original icon
        for (uint32_t i = 0; i < count; ++i) {
            auto* g = sub+16+i*12;
            if (be32(g) == cp && be32(g+4) == cp) put32(g+8, id);
        }
    };
    const uint32_t actions[] = {0xE0E0,0xE0E1,0xE0E2,0xE0E3,0xE0E4,0xE0E5,0xE0E6,0xE0E7,
        0xE0EF,0xE0F0,0,0,0xE0AE,0xE0AD,0xE0AB,0xE0AC};
    for (size_t a = 0; a < 16; ++a) {
        const uint32_t target = kind == plat::ControllerKind::Keyboard ? binding_key_code(s, a, keys[a])
            : 0xE200 + uint32_t(overlay::controller_binding(s, a));
        if (actions[a]) replace(actions[a], target);
        if (a < 8) replace(0xE0A0+uint32_t(a), target);
    }
    put32(cmap_record+4, checksum(cmap, cmap_size));
    put32(head+8, 0);
    put32(head+8, 0xB1B0AFBAu-checksum(out.data(), out.size()));
    return out;
}
}
