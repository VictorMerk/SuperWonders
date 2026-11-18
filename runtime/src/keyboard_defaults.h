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
// Wonder default scheme: B jump, Y dash, A use item/confirm, X greet.
// Named PC actions use these canonical slots; keyboard_actions.h translates combinations/layouts.
// Values are Win32 virtual keys or SDL's stable USB-HID scancodes, without SDK dependencies.
constexpr KeyboardBindings recommended_keyboard_bindings(KeyboardCodes codes=native_keyboard_codes()) {
    if(codes==KeyboardCodes::Windows)
        return {'F',0x20,'Q',0x10,'Z','E',0x11,0,0x0d,0x08,0,0,'A','W','D','S'};
    return {9,44,20,225,29,8,224,0,40,42,0,0,4,26,7,22};
}
constexpr KeyboardBindings legacy_keyboard_bindings(KeyboardCodes codes) {
    if(codes==KeyboardCodes::Windows)
        return {0x20,0x10,'J','K','Q','E','Z','C',0x0d,0x08,'M','N',0x25,0x26,0x27,0x28};
    return {44,225,13,14,20,8,29,6,40,42,16,17,80,82,79,81};
}
// First PC preset (version 1), retained solely to recognize and migrate untouched files.
constexpr KeyboardBindings version_one_keyboard_bindings(KeyboardCodes codes) {
    if(codes==KeyboardCodes::Windows)
        return {0x20,'J',0x11,0x10,'Q','E','Z','C',0x0d,0x08,'M','N',0x25,0x26,0x27,0x28};
    return {44,13,224,225,20,8,29,6,40,42,16,17,80,82,79,81};
}
inline bool migrate_keyboard_defaults(KeyboardBindings& keys,int layout_version,KeyboardCodes codes=native_keyboard_codes()) {
    if(layout_version>=2)return false;
    const bool original=layout_version<1 && keys==legacy_keyboard_bindings(codes);
    const bool previous=keys==version_one_keyboard_bindings(codes);
    if(!original && !previous)return false;
    keys=recommended_keyboard_bindings(codes);return true;
}
constexpr int default_standee_key(KeyboardCodes codes=native_keyboard_codes()) {return codes==KeyboardCodes::Windows?'V':25;}
template<class Query> bool keyboard_key_held(int binding,KeyboardCodes codes,Query query) {
    if(binding<=0)return false;
    if(query(binding))return true;
    if(codes==KeyboardCodes::SDL) {
        if(binding==225)return query(229); // either Shift, unless a specific right key was explicitly bound
        if(binding==224)return query(228); // either Ctrl
    }
    return false;
}
}
