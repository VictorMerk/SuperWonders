#pragma once
#include <algorithm>
#include "imgui.h"
#include "controllers.h"
#include "overlay/settings.h"
#include "overlay/overlay_ui.h"
#include "button_font.h"
namespace nx::overlay {
// A scalable controller silhouette. Physical buttons select a mapping; mapped input lights them.
inline int controller_diagram(const Settings& s,plat::ControllerKind kind,uint64_t buttons,int selected) {
    const auto origin=ImGui::GetCursorScreenPos();
    const float width=ImGui::GetContentRegionAvail().x;
    const float scale=std::min(width/440.f,1.35f),height=240*scale;
    const ImVec2 o{origin.x+(width-440*scale)*.5f,origin.y};
    auto point=[&](float x,float y){return ImVec2{o.x+x*scale,o.y+y*scale};};
    auto* dl=ImGui::GetWindowDrawList();
    const ImU32 shell=IM_COL32(48,57,72,255),edge=IM_COL32(111,125,147,255);
    dl->PathLineTo(point(94,44));dl->PathLineTo(point(346,44));
    dl->PathBezierCubicCurveTo(point(395,44),point(414,153),point(413,195));
    dl->PathBezierCubicCurveTo(point(407,236),point(369,232),point(322,155));
    dl->PathLineTo(point(118,155));
    dl->PathBezierCubicCurveTo(point(71,232),point(33,236),point(27,195));
    dl->PathBezierCubicCurveTo(point(26,153),point(45,44),point(94,44));
    dl->PathFillConcave(shell);
    dl->AddRect(point(105,53),point(335,149),edge,22*scale,0,scale);
    const ImVec2 pad=kind==plat::ControllerKind::PlayStation?point(110,105):point(146,150);
    const ImVec2 ls=kind==plat::ControllerKind::PlayStation?point(155,161):point(110,105);
    auto circle=[&](ImVec2 c,float radius,ImU32 color){dl->AddCircleFilled(c,radius*scale,color,32);};
    circle(ls,22,IM_COL32(18,24,34,255));circle(point(285,161),22,IM_COL32(18,24,34,255));
    constexpr uint64_t bits[]={plat::kA,plat::kB,plat::kX,plat::kY,plat::kL,plat::kR,plat::kZL,plat::kZR,plat::kPlus,plat::kMinus,plat::kStickL,plat::kStickR,plat::kLeft,plat::kUp,plat::kRight,plat::kDown};
    // Physical enum order: south, east, west, north, shoulders, triggers, start/back, sticks, d-pad.
    ImVec2 positions[]={point(330,124),point(355,99),point(305,99),point(330,74),point(105,39),point(335,39),point(105,13),point(335,13),point(246,96),point(194,96),ls,point(285,161),{pad.x-17*scale,pad.y},{pad.x,pad.y-17*scale},{pad.x+17*scale,pad.y},{pad.x,pad.y+17*scale}};
    int clicked=-1;
    for(int physical=0;physical<16;++physical) {
        bool held=false;for(size_t action=0;action<16;++action)if(controller_binding(s,action)==physical && (buttons&bits[action]))held=true;
        const auto pos=positions[physical];const float radius=(physical>=12?10.f:physical>=8?12.f:15.f)*scale;
        dl->AddCircleFilled(pos,radius,held?ui::theme().accent:IM_COL32(24,31,44,255),24);
        if(selected==physical)dl->AddCircle(pos,radius+3*scale,ui::theme().accent,24,2*scale);
        const char* name=physical_button_name(kind,physical);
        if(physical>=12)name=physical==12?"<":physical==13?"^":physical==14?">":"v";
        if(physical==10 || physical==11)name=physical==10?"LS":"RS";
        if(physical==8 || physical==9)name=physical==8?"+":"-";
        if(kind==plat::ControllerKind::PlayStation && physical<4) {
            const float d=6*scale;const ImU32 ink=ui::theme().text;
            if(physical==0){dl->AddLine({pos.x-d,pos.y-d},{pos.x+d,pos.y+d},ink,scale);dl->AddLine({pos.x-d,pos.y+d},{pos.x+d,pos.y-d},ink,scale);}
            if(physical==1)dl->AddCircle(pos,d,ink,20,scale);
            if(physical==2)dl->AddRect({pos.x-d,pos.y-d},{pos.x+d,pos.y+d},ink,0,0,scale);
            if(physical==3)dl->AddTriangle({pos.x,pos.y-d},{pos.x+d,pos.y+d},{pos.x-d,pos.y+d},ink,scale);
            name="";
        }
        const float font=std::min(ImGui::GetFontSize(),12.f*scale);
        const auto size=ImGui::GetFont()->CalcTextSizeA(font,1000,0,name);
        dl->AddText(ImGui::GetFont(),font,{pos.x-size.x*.5f,pos.y-size.y*.5f},held?IM_COL32(12,16,24,255):ui::theme().text,name);
        ImGui::SetCursorScreenPos({pos.x-radius,pos.y-radius});ImGui::PushID(physical);
        if(ImGui::InvisibleButton("##controller_button",{2*radius,2*radius}))clicked=physical;
        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s: select to remap",physical_button_name(kind,physical));
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(origin);ImGui::Dummy({width,height});return clicked;
}
}
