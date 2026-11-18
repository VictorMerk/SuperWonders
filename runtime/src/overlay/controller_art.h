#pragma once
#include <array>
#include <string_view>
#include "imgui.h"
#include "controllers.h"
namespace nx::overlay {
enum class ControllerArtModel { DualSense, DualShock4, XboxSeries, XboxOne, Xbox360, SwitchPro, JoyConPair, SteamController, SteamDeck, Count };
constexpr int kControllerArtCount=int(ControllerArtModel::Count);
struct ControllerArtLayout { plat::ControllerKind kind; std::array<ImVec2,16> buttons; bool shoulder_chips=false; };
const char* controller_art_name(ControllerArtModel model);
ControllerArtModel detect_controller_art(std::string_view name,plat::ControllerKind fallback);
const ControllerArtLayout& controller_art_layout(ControllerArtModel model);
// Register/fill colored atlas rectangles once after fonts are added, before backend upload.
void init_controller_art(ImFontAtlas* atlas);
bool draw_controller_art(ControllerArtModel model,ImDrawList* draw,ImVec2 top_left,ImVec2 bottom_right,ImU32 tint);
float controller_art_aspect(ControllerArtModel model);
}
