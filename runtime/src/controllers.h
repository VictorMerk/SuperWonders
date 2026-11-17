#pragma once
#include "platform.h"
#include <array>
#include <string>

namespace nx::plat {
constexpr uint32_t kPlayerCount = 4;
enum class ControllerKind { Keyboard, Nintendo, Xbox, PlayStation };
// Stable slots: unplugging one player never shifts another player's controller into their place.
bool read_controller(uint32_t player, PadState& out, std::array<bool, 16>* physical = nullptr);
bool player_connected(uint32_t player);  // player 1 also has the keyboard
bool read_player_pad(uint32_t player, PadState& out);
void controller_binding(size_t action, int physical);
void controller_rumble_strength(int percent);
void controller_keyboard_separate(bool separate);
void controller_force_primary(bool force);  // LAN: every PC contributes its primary local device
void rumble_player(uint32_t player, float low, float high);
ControllerKind controller_kind(uint32_t player);
std::string controller_name(uint32_t player);
void close_controllers();
void watch_controller_connections(void (*changed)(uint32_t player));
}
