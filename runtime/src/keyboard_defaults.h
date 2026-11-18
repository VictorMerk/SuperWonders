#pragma once
#include <array>
#include <cstddef>

namespace nx::input {
enum class KeyboardCodes { Windows, SDL };
constexpr KeyboardCodes native_keyboard_codes() {
#if defined(_WIN32)
    return KeyboardCodes::Windows;
#else
    return KeyboardCodes::SDL;
#endif
}
using KeyboardBindings=std::array<int,16>;
// NX action order: A, B, X, Y, L, R, ZL, ZR, Plus, Minus, stick clicks, D-pad.
// Wonder: A/B jump, X/Y dash. Space confirms/jumps; Shift runs; J/Ctrl are alternates.
// Values are Win32 virtual keys or SDL's stable USB-HID scancodes, without SDK dependencies.
constexpr KeyboardBindings recommended_keyboard_bindings(KeyboardCodes codes=native_keyboard_codes()) {
    if(codes==KeyboardCodes::Windows)
        return {0x20,'J',0x11,0x10,'Q','E','Z','C',0x0d,0x08,'M','N',0x25,0x26,0x27,0x28};
    return {44,13,224,225,20,8,29,6,40,42,16,17,80,82,79,81};
}
constexpr KeyboardBindings legacy_keyboard_bindings(KeyboardCodes codes) {
    if(codes==KeyboardCodes::Windows)
        return {0x20,0x10,'J','K','Q','E','Z','C',0x0d,0x08,'M','N',0x25,0x26,0x27,0x28};
    return {44,225,13,14,20,8,29,6,40,42,16,17,80,82,79,81};
}
// Only an untouched pre-versioned preset is migrated. Any custom key preserves the entire layout.
inline bool migrate_keyboard_defaults(KeyboardBindings& keys,int layout_version,KeyboardCodes codes=native_keyboard_codes()) {
    if(layout_version>=1 || keys!=legacy_keyboard_bindings(codes))return false;
    keys=recommended_keyboard_bindings(codes);return true;
}
template<class Query> bool keyboard_key_held(int binding,KeyboardCodes codes,Query query) {
    if(query(binding))return true;
    if(codes==KeyboardCodes::SDL) {
        if(binding==225)return query(229); // either Shift, unless a specific right key was explicitly bound
        if(binding==224)return query(228); // either Ctrl
    }
    return false;
}
}
