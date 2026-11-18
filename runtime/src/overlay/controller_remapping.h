#pragma once
#include "overlay/controller_diagram.h"
#include "keyboard_actions.h"
namespace nx::overlay {
void settings_changed(bool persist);
uint64_t pad_buttons();
struct ControllerRemapState {int action=int(CtlB),capture=-1;std::array<bool,16> previous{};};
inline ControllerRemapState& controller_remap_state(){static ControllerRemapState state;return state;}
inline void cancel_controller_remapping(){controller_remap_state().capture=-1;}
inline void controller_remapping(Settings& s,bool appearing,bool open=true){
 auto& state=controller_remap_state();
 if(appearing||!open)state.capture=-1;
 ImGui::BeginDisabled(!open);
 const auto device=plat::controller_kind(0);const auto name=plat::controller_name(0);
 const char* models[]={"Automatic","DualSense (PS5)","DualShock 4 (PS4)","Xbox Series","Xbox One","Xbox 360","Nintendo Switch Pro","Paired Joy-Cons","Steam Controller","Steam Deck"};
 if(device==plat::ControllerKind::Keyboard)ImGui::TextDisabled("Preview only: no gamepad detected.");
 const auto hint=plat::controller_model_hint(0);
 const auto model=s.controller_artwork?ControllerArtModel(s.controller_artwork-1):detect_controller_art(hint.empty()?name:hint,device);
 const auto kind=controller_art_layout(model).kind;
 ImGui::TextWrapped("%s | %s",name.c_str(),controller_art_name(model));
 if(ImGui::SmallButton("Artwork & game prompts")){state.capture=-1;ImGui::OpenPopup("##art_options");}
 if(ImGui::BeginPopup("##art_options")){
  ImGui::TextUnformatted("Model changes are visual only.");
  ImGui::SetNextItemWidth(260);if(ImGui::Combo("Model",&s.controller_artwork,models,10))settings_changed(true);
  const char* prompts[]={"Automatic at startup","Keyboard","Nintendo","Xbox","PlayStation"};
  ImGui::SetNextItemWidth(260);if(ImGui::Combo("Game prompts",&s.button_icons,prompts,5))settings_changed(true);
  const char* layouts[]={"B jump / Y dash","A jump / B dash"};
  ImGui::SetNextItemWidth(260);if(ImGui::Combo("Wonder game layout",&s.keyboard_game_layout,layouts,2))settings_changed(true);
  ImGui::TextWrapped("Match Wonder's own jump/dash option. This updates action labels and keyboard translation, not your save or controller mappings. Restart to refresh prompts.");
  ImGui::EndPopup();
 }
 std::array<bool,16> physical{};plat::PadState raw{};plat::read_controller(0,raw,&physical);
 auto bind=[&](int button){if(assign_controller_button(s,size_t(state.action),button))settings_changed(true);state.capture=-1;};
 bool just_bound=false;
 if(state.capture>=0)for(int p=0;p<16;++p)if(physical[p]&&!state.previous[p]){state.action=state.capture;bind(p);just_bound=true;break;}
 state.previous=physical;
 auto action_name=[&](int game_action){const auto slot=input::canonical_keyboard_slot(size_t(game_action),s.keyboard_game_layout);const char* named=input::keyboard_action_name(slot);
  static const char* extra[]={"L shoulder","R trigger / Spin","Left stick click","Right stick click"};
  if(game_action==6)return "ZL / Ground pound";
  if(named)return named;return game_action==4?extra[0]:game_action==7?extra[1]:game_action==10?extra[2]:extra[3];};
 if(state.capture>=0){ImGui::TextWrapped("Listening for: %s. Press a controller button.",action_name(state.capture));ImGui::SameLine();if(ImGui::Button("Cancel listening"))state.capture=-1;}
 else ImGui::TextWrapped("Select an action to press its new button, or click the artwork to change the highlighted action.");
 const ImVec2 avail=ImGui::GetContentRegionAvail();const bool wide=avail.x>=560.f;
 const float art_width=wide?avail.x*.53f:avail.x;
 ImGui::BeginDisabled(just_bound);
 ImGui::BeginChild("##controller_picture",{art_width,wide?avail.y:std::max(60.f,avail.y*.42f)},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar);
 ImGui::TextWrapped("Editing: %s",action_name(state.action));
 const int selected=controller_binding(s,state.action);
 const int clicked=controller_diagram(s,kind,pad_buttons(),selected,model,std::max(32.f,ImGui::GetContentRegionAvail().y-4));
 if(clicked>=0)bind(clicked);
 ImGui::EndChild();if(wide)ImGui::SameLine();
 ImGui::BeginChild("##controller_action_list",{0,0},ImGuiChildFlags_NavFlattened);
 if(ImGui::BeginTable("##controller_actions",2,ImGuiTableFlags_SizingStretchProp)){
  ImGui::TableSetupColumn("Action",ImGuiTableColumnFlags_WidthStretch);ImGui::TableSetupColumn("Button",ImGuiTableColumnFlags_WidthFixed,100.f);
  constexpr int order[]={1,3,5,6,0,2,8,9,12,13,14,15,4,7,10,11};
  for(int a:order){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::PushID(a);
   if(ImGui::Selectable(action_name(a),state.action==a,ImGuiSelectableFlags_SpanAllColumns,{0,ImGui::GetTextLineHeight()+10})){state.action=a;state.capture=a;}
   ImGui::TableNextColumn();ImGui::TextUnformatted(state.capture==a?"Press button...":physical_button_name(kind,controller_binding(s,a)));ImGui::PopID();
  }ImGui::EndTable();
 }
 ImGui::Separator();
 const char* presets[]={"Xbox / PS: bottom confirms","Nintendo: right confirms","PlayStation: Circle confirms"};
 if(ImGui::Combo("Layout preset",&s.controller_preset,presets,3))settings_changed(true);
 if(ImGui::Button("Reset controller mappings")){s.controller_custom_mask=0;state.capture=-1;settings_changed(true);}
 if(ImGui::SliderInt("Rumble",&s.rumble,0,100,"%d%%"))settings_changed(true);
 ImGui::TextWrapped("Occupied buttons swap assignments. Keys and mappings apply immediately. Restart to refresh game prompts. Match the game's jump/dash layout in Keyboard remapping. Extra inputs depend on the controller and driver.");
 ImGui::EndChild();ImGui::EndDisabled();ImGui::EndDisabled();
}
}
