#pragma once
#include "overlay/controller_diagram.h"
#include "keyboard_actions.h"
#include "pc_menu_layout.h"
#include <string>
namespace nx::overlay {
void settings_changed(bool persist);
uint64_t pad_buttons();
struct ControllerRemapState {int action=int(CtlB),capture=-1;std::array<bool,16> previous{};};
inline ControllerRemapState& controller_remap_state(){static ControllerRemapState state;return state;}
inline void cancel_controller_remapping(){controller_remap_state().capture=-1;}
inline void controller_remapping(Settings& s,bool appearing,bool open=true) {
    auto& state=controller_remap_state();
    if(appearing||!open) state.capture=-1;
    ImGui::BeginDisabled(!open);
    const float unit=plat::menu_layout::unit(ImGui::GetFontSize());
    const float target=plat::menu_layout::target_height(ImGui::GetFontSize());
    const bool compact=ImGui::GetContentRegionAvail().y<400.f*unit;
    const auto device=plat::controller_kind(0);
    const auto name=plat::controller_name(0);
    const char* models[]={"Automatic","DualSense (PS5)","DualShock 4 (PS4)","Xbox Series","Xbox One","Xbox 360","Nintendo Switch Pro","Paired Joy-Cons","Steam Controller","Steam Deck"};
    const auto hint=plat::controller_model_hint(0);
    const auto model=s.controller_artwork?ControllerArtModel(s.controller_artwork-1):detect_controller_art(hint.empty()?name:hint,device);
    const auto kind=controller_art_layout(model).kind;
    if(!compact)ImGui::TextWrapped("%s | %s",name.c_str(),controller_art_name(model));
    const bool connected=device!=plat::ControllerKind::Keyboard;
    if(!connected&&!compact) ImGui::TextDisabled("Preview only: connect a gamepad to listen for its buttons.");
    auto options=[&] {
        if(ui::navigation_button("Artwork & game prompts",false,std::min(ImGui::GetContentRegionAvail().x,330.f*unit))) {
            state.capture=-1;ImGui::OpenPopup("##art_options");
        }
    };
    if(!compact)options();
    auto popup=[&] {
    const auto display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(480.f*unit,std::max(1.f,display.x-24)),0},ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints({0,0},{std::max(1.f,display.x-24),std::max(1.f,display.y-24)});
    if(ImGui::BeginPopup("##art_options")) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+std::min(430.f*unit,display.x-60));
        ImGui::TextUnformatted("Model changes are visual only.");
        auto combo=[&](const char* label,int* value,const char* const* values,int count) {
            ImGui::TextUnformatted(label);ImGui::SetNextItemWidth(-1);
            ImGui::PushID(label);
            const bool changed=ImGui::Combo("##value",value,values,count);
            ImGui::PopID();if(changed)settings_changed(true);
        };
        combo("Model",&s.controller_artwork,models,10);
        const char* prompts[]={"Automatic at startup","Keyboard","Nintendo","Xbox","PlayStation"};
        combo("Game prompts",&s.button_icons,prompts,5);
        const char* layouts[]={"B jump / Y dash","A jump / B dash"};
        combo("Wonder game layout",&s.keyboard_game_layout,layouts,2);
        ImGui::TextWrapped("Match Wonder's own jump/dash option. This updates action labels and keyboard translation, not your save or controller mappings. Restart to refresh prompts.");
        ImGui::PopTextWrapPos();ImGui::EndPopup();
    }
    };
    if(!compact)popup();
    std::array<bool,16> physical{};plat::PadState raw{};
    plat::read_controller(0,raw,&physical);
    auto bind=[&](int button) {
        if(assign_controller_button(s,size_t(state.action),button))settings_changed(true);
        state.capture=-1;
    };
    bool just_bound=false;
    if(state.capture>=0&&connected)
        for(int p=0;p<16;++p) if(physical[p]&&!state.previous[p]) {
            state.action=state.capture;bind(p);just_bound=true;break;
        }
    state.previous=physical;
    auto action_name=[&](int action) {
        const auto slot=input::canonical_keyboard_slot(size_t(action),s.keyboard_game_layout);
        const char* named=input::keyboard_action_name(slot);
        static const char* extra[]={"L shoulder","R trigger / Spin","Left stick click","Right stick click"};
        if(action==6)return "ZL / Ground pound";
        if(named)return named;
        return action==4?extra[0]:action==7?extra[1]:action==10?extra[2]:extra[3];
    };
    if(state.capture>=0) {
        ImGui::PushStyleColor(ImGuiCol_Text,ImGui::ColorConvertU32ToFloat4(ui::theme().accent));
        ImGui::TextWrapped("Listening for: %s. Press a controller button.",action_name(state.capture));
        ImGui::PopStyleColor();
        if(ImGui::Button("Cancel listening",{0,target}))state.capture=-1;
    } else if(!compact)ImGui::TextWrapped("Choose an action below, then press its new button. The preview can also assign a button to the selected action.");

    const ImVec2 avail=ImGui::GetContentRegionAvail();
    // Keep readable action/value columns instead of forcing a diagram beside a
    // 250-pixel list. Compact windows give the action list priority.
    const bool wide=avail.x>=720.f*unit&&avail.y>=220.f*unit;
    const bool short_window=avail.y<460.f*unit;
    ImGui::BeginDisabled(just_bound);
    const bool show_art=wide||(!compact&&(!short_window||ImGui::CollapsingHeader("Controller preview")));
    if(show_art) {
        const float art_width=wide?avail.x*.40f:avail.x;
        const float art_height=wide?avail.y:std::min(220.f*unit,std::max(90.f,avail.y*.32f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg,ImGui::ColorConvertU32ToFloat4(ui::theme().back_top));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(14*unit,14*unit));
        ImGui::BeginChild("##controller_picture",{art_width,art_height},ImGuiChildFlags_AlwaysUseWindowPadding,ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();ImGui::PopStyleColor();
        ImGui::TextWrapped("Editing: %s",action_name(state.action));
        const int clicked=controller_diagram(s,kind,pad_buttons(),controller_binding(s,state.action),model,
            std::max(32.f,ImGui::GetContentRegionAvail().y-4));
        if(clicked>=0)bind(clicked);
        ImGui::EndChild();
        if(wide)ImGui::SameLine();
    }
    ImGui::BeginChild("##controller_action_list",{0,0},ImGuiChildFlags_NavFlattened);
    const bool stacked=ImGui::GetContentRegionAvail().x<480.f*unit;
    if(ImGui::BeginTable("##controller_actions",stacked?1:2,ImGuiTableFlags_SizingStretchProp)) {
        if(!stacked) {
            ImGui::TableSetupColumn("Action",ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Button",ImGuiTableColumnFlags_WidthFixed,160.f*unit);
            ImGui::TableHeadersRow();
        }
        constexpr int order[]={1,3,5,6,0,2,8,9,12,13,14,15,4,7,10,11};
        for(int action:order) {
            ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::PushID(action);
            const char* value=state.capture==action?"Press button...":physical_button_name(kind,controller_binding(s,action));
            std::string label=action_name(action);
            if(stacked)label+="\n"+std::string(value);
            label+="###action";
            const float row_height=stacked?std::max(target,ImGui::GetTextLineHeight()*2+12*unit):target;
            if(ImGui::Selectable(label.c_str(),state.action==action,ImGuiSelectableFlags_SpanAllColumns,{0,row_height})) {
                state.action=action;state.capture=connected?action:-1;
            }
            if(!stacked){ImGui::TableNextColumn();ImGui::TextWrapped("%s",value);}
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
    const char* presets[]={"Xbox / PS: bottom confirms","Nintendo: right confirms","PlayStation: Circle confirms"};
    ImGui::TextUnformatted("Layout preset");ImGui::SetNextItemWidth(-1);
    if(ImGui::Combo("##preset",&s.controller_preset,presets,3)){state.capture=-1;settings_changed(true);}
    if(ImGui::Button("Reset controller mappings",{-1,target})) {
        s.controller_custom_mask=0;state.capture=-1;settings_changed(true);
    }
    ImGui::TextUnformatted("Rumble");ImGui::SetNextItemWidth(-1);
    if(ImGui::SliderInt("##rumble",&s.rumble,0,100,"%d%%"))settings_changed(true);
    ImGui::TextWrapped("Occupied buttons swap assignments. Bindings apply immediately; restart to refresh game prompts. Extra inputs depend on the controller and driver.");
    if(compact) {
        options();popup();ImGui::TextWrapped("%s | %s",name.c_str(),controller_art_name(model));
        if(!connected)ImGui::TextWrapped("Connect a gamepad to listen for its buttons.");
        if(ImGui::CollapsingHeader("Controller preview")) {
            const int clicked=controller_diagram(s,kind,pad_buttons(),controller_binding(s,state.action),model,180.f*unit);
            if(clicked>=0)bind(clicked);
        }
    }
    ImGui::EndChild();ImGui::EndDisabled();ImGui::EndDisabled();
}
}
