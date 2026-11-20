#pragma once
#include <algorithm>
#include "overlay/controller_art.h"
#include "overlay/settings.h"
#include "overlay/overlay_ui.h"
#include "button_font.h"
namespace nx::overlay {
// Art and hit regions share the same aspect-preserving transform.
inline int controller_diagram(const Settings& s,plat::ControllerKind kind,uint64_t buttons,int selected,
                             ControllerArtModel model=ControllerArtModel::Count,float max_height=320.f){
 if(model==ControllerArtModel::Count)model=detect_controller_art("",kind);
 const auto origin=ImGui::GetCursorScreenPos();const float avail=ImGui::GetContentRegionAvail().x;
 const auto& layout=controller_art_layout(model);
 const float chip_height=std::max(28.f,ImGui::GetFontSize()*1.8f);
 const float gap=layout.shoulder_chips?chip_height+8.f:0.f;
 const float aspect=controller_art_aspect(model);const float h=std::max(1.f,std::min(avail/aspect,std::max(1.f,max_height-gap)));
 const float w=h*aspect;const ImVec2 p{origin.x+(avail-w)*.5f,origin.y+gap},q{p.x+w,p.y+h};auto* draw=ImGui::GetWindowDrawList();
 if(!draw_controller_art(model,draw,p,q,ImGui::GetColorU32(IM_COL32_WHITE)))ImGui::TextDisabled("Controller artwork unavailable; use the button picker.");
 if(model==ControllerArtModel::SteamDeck){
  draw->AddRectFilled({p.x+w*.24f,p.y+h*.12f},{p.x+w*.755f,p.y+h*.914f},ui::theme().back_top);
  const char* title="Steam Deck";const auto text=ImGui::CalcTextSize(title);
  draw->AddText({p.x+w*.494f-text.x*.5f,p.y+h*.51f-text.y*.5f},ui::theme().text_dim,title);
 }
 constexpr uint64_t bits[]={plat::kA,plat::kB,plat::kX,plat::kY,plat::kL,plat::kR,plat::kZL,plat::kZR,plat::kPlus,plat::kMinus,plat::kStickL,plat::kStickR,plat::kLeft,plat::kUp,plat::kRight,plat::kDown};
 int clicked=-1;ImGui::PushID("controller_art_buttons");
 for(int physical=0;physical<16;++physical){
  bool held=false;for(size_t action=0;action<16;++action)if(controller_binding(s,action)==physical&&(buttons&bits[action]))held=true;
  auto uv=layout.buttons[physical];ImVec2 center{p.x+uv.x*w,p.y+uv.y*h};
  const float radius=std::max(5.f,w*(physical>=12?.018f:physical==10||physical==11?.04f:.026f));
  ImVec2 hit0{center.x-radius,center.y-radius},hit1{center.x+radius,center.y+radius};
  const bool chip=layout.shoulder_chips && physical>=4 && physical<=7;
  if(chip){const int order=physical==6?0:physical==4?1:physical==5?2:3;
   center={origin.x+(order+.5f)*avail/4,origin.y+chip_height*.5f};
   hit0={origin.x+order*avail/4+4,origin.y};
   hit1={origin.x+(order+1)*avail/4-4,origin.y+chip_height};
   draw->AddRectFilled(hit0,hit1,held?ui::theme().accent:ui::theme().card,4);
   const char* label=physical_button_name(layout.kind,physical);const auto text=ImGui::CalcTextSize(label);
   draw->AddText({center.x-text.x*.5f,center.y-text.y*.5f},held?IM_COL32(16,24,29,255):ui::theme().text,label);
  }else if(held)draw->AddCircleFilled(center,radius,ui::fade(ui::theme().accent,.38f),24);
  if(held||selected==physical){if(chip)draw->AddRect(hit0,hit1,ui::theme().accent,6,0,2);else draw->AddCircle(center,radius+2,ui::fade(ui::theme().accent),32,2);}
  ImGui::SetCursorScreenPos(hit0);ImGui::PushID(physical);
  if(ImGui::InvisibleButton("##button",{std::max(1.f,hit1.x-hit0.x),std::max(1.f,hit1.y-hit0.y)},ImGuiButtonFlags_EnableNav))clicked=physical;
  if(ImGui::IsItemFocused()&&ImGui::GetIO().NavVisible)
   draw->AddRect(hit0,hit1,ui::theme().accent,4,0,2);
  if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))ImGui::SetTooltip("%s: assign to selected action",physical_button_name(layout.kind,physical));
  ImGui::PopID();
 }
 ImGui::PopID();ImGui::SetCursorScreenPos(origin);ImGui::Dummy({avail,h+gap+4});return clicked;
}
}
